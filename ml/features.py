"""Feature extraction for OpenStreetMap ways.

Turns each <way> in an .osm file into a row of numeric features that a
scikit-learn model can consume. Any tag that directly encodes a speed
(maxspeed, maxspeed:hgv, maxspeed:trailer, ...) is excluded from the
features to avoid label leakage.
"""

import math
import re
import xml.etree.ElementTree as ET

import pandas as pd

# Road classes we expect to appear in training data. Unknown classes are
# one-hot encoded as all zeros.
HIGHWAY_CLASSES = [
    "motorway", "trunk", "primary", "secondary", "tertiary",
    "residential", "unclassified", "service", "living_street",
]

NAME_SUFFIXES = {
    "street": "street", "st": "street",
    "road": "road", "rd": "road",
    "avenue": "avenue", "ave": "avenue",
    "boulevard": "boulevard", "blvd": "boulevard",
    "drive": "drive", "dr": "drive",
    "way": "way",
    "court": "court", "ct": "court",
    "lane": "lane", "ln": "lane",
    "place": "place", "pl": "place",
    "circle": "circle", "cir": "circle",
    "terrace": "terrace",
    "highway": "highway", "hwy": "highway",
    "loop": "loop",
}
SUFFIX_CATEGORIES = sorted(set(NAME_SUFFIXES.values()))


def _haversine_miles(lat1, lon1, lat2, lon2):
    r = 3959.88
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def parse_speed_mph(value):
    """'25 mph' -> 25, '40' -> 40 (OSM default unit is km/h), else None."""
    if value is None:
        return None
    m = re.search(r"(\d+(?:\.\d+)?)", value)
    if not m:
        return None
    speed = float(m.group(1))
    if "mph" not in value.lower():
        speed = speed * 0.621371
    return int(round(speed))


def load_ways(osm_path):
    """Return a list of dicts: {id, tags, length_mi, node_count, lat, lon}."""
    root = ET.parse(osm_path).getroot()
    nodes = {
        n.get("id"): (float(n.get("lat")), float(n.get("lon")))
        for n in root.findall("node")
    }
    ways = []
    for w in root.findall("way"):
        tags = {t.get("k"): t.get("v") for t in w.findall("tag")}
        refs = [nd.get("ref") for nd in w.findall("nd") if nd.get("ref") in nodes]
        coords = [nodes[r] for r in refs]
        length = sum(
            _haversine_miles(*coords[i - 1], *coords[i]) for i in range(1, len(coords))
        )
        lat = sum(c[0] for c in coords) / len(coords) if coords else 0.0
        lon = sum(c[1] for c in coords) / len(coords) if coords else 0.0
        ways.append({
            "id": int(w.get("id")),
            "tags": tags,
            "length_mi": length,
            "node_count": len(refs),
            "lat": lat,
            "lon": lon,
        })
    return ways


def _name_suffix(name):
    if not name:
        return None
    last = re.sub(r"[^a-z]", "", name.strip().split()[-1].lower())
    return NAME_SUFFIXES.get(last)


def _yes(tags, key):
    return 1 if tags.get(key, "").lower() in ("yes", "true", "1", "-1") else 0


def way_features(way):
    tags = way["tags"]
    hw = tags.get("highway", "")
    name = tags.get("name")
    lanes_match = re.match(r"\d+", tags.get("lanes", ""))
    row = {
        "lanes": int(lanes_match.group()) if lanes_match else -1,
        "has_lanes": 1 if lanes_match else 0,
        "oneway": _yes(tags, "oneway"),
        "has_name": 1 if name else 0,
        "has_ref": 1 if "ref" in tags else 0,
        "bridge": _yes(tags, "bridge"),
        "layer": int(tags["layer"]) if tags.get("layer", "").lstrip("-").isdigit() else 0,
        "paved": 1 if tags.get("surface", "") in ("asphalt", "paved", "concrete") else 0,
        "has_surface": 1 if "surface" in tags else 0,
        "has_cycleway": 1 if any(k.startswith("cycleway") for k in tags) else 0,
        "bicycle_no": 1 if tags.get("bicycle") == "no" else 0,
        "hgv_tagged": 1 if "hgv" in tags else 0,
        "has_destination": 1 if any(k.startswith("destination") for k in tags) else 0,
        "access_restricted": 1 if tags.get("access") in ("private", "no", "destination") else 0,
        "roundabout": 1 if tags.get("junction") == "roundabout" else 0,
        "traffic_calming": 1 if "traffic_calming" in tags else 0,
        "length_mi": way["length_mi"],
        "node_count": way["node_count"],
        "lat": way["lat"],
        "lon": way["lon"],
    }
    for cls in HIGHWAY_CLASSES:
        row[f"hw_{cls}"] = 1 if hw == cls else 0
    suffix = _name_suffix(name)
    for s in SUFFIX_CATEGORIES:
        row[f"suffix_{s}"] = 1 if suffix == s else 0
    return row


def build_frame(osm_path):
    """DataFrame with features plus id, highway, name and speed_mph label (or NaN)."""
    rows = []
    for way in load_ways(osm_path):
        tags = way["tags"]
        if "highway" not in tags:
            continue
        row = way_features(way)
        row["id"] = way["id"]
        row["highway"] = tags["highway"]
        row["name"] = tags.get("name") or f"__unnamed_{way['id']}"
        row["speed_mph"] = parse_speed_mph(tags.get("maxspeed"))
        rows.append(row)
    return pd.DataFrame(rows)


FEATURE_COLUMNS = None  # filled lazily


def feature_columns(df):
    return [c for c in df.columns if c not in ("id", "highway", "name", "speed_mph")]

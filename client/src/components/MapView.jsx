import { useEffect, useMemo, useRef, useState } from "react";

const fallbackCenter = {
  latitude: 38.5425,
  longitude: -121.7565
};

function getBounds(points) {
  if (!points.length) {
    return null;
  }

  return points.reduce(
    (bounds, [longitude, latitude]) => ({
      west: Math.min(bounds.west, longitude),
      east: Math.max(bounds.east, longitude),
      south: Math.min(bounds.south, latitude),
      north: Math.max(bounds.north, latitude)
    }),
    {
      west: points[0][0],
      east: points[0][0],
      south: points[0][1],
      north: points[0][1]
    }
  );
}

function createMarkerIcon(role) {
  return {
    html: `<span class="map-marker ${role}" aria-hidden="true"><span>${role === "start" ? "A" : "B"}</span></span>`,
    className: "leaflet-marker-shell",
    iconSize: [34, 34],
    iconAnchor: [17, 41]
  };
}

export default function MapView({ startLocation, endLocation, route, loading }) {
  const containerRef = useRef(null);
  const mapRef = useRef(null);
  const leafletRef = useRef(null);
  const startMarkerRef = useRef(null);
  const endMarkerRef = useRef(null);
  const routeLineRef = useRef(null);
  const glowLineRef = useRef(null);
  const [mapReady, setMapReady] = useState(false);
  const interactiveReady = import.meta.env.MODE !== "test";

  const routeCoordinates = useMemo(() => route?.geometry?.coordinates || [], [route]);
  const selectedPoints = useMemo(() => {
    const points = [];

    if (startLocation?.coordinates) {
      points.push([startLocation.coordinates.longitude, startLocation.coordinates.latitude]);
    }

    if (endLocation?.coordinates) {
      points.push([endLocation.coordinates.longitude, endLocation.coordinates.latitude]);
    }

    return points;
  }, [endLocation?.coordinates, startLocation?.coordinates]);

  useEffect(() => {
    if (!interactiveReady || !containerRef.current) {
      return undefined;
    }

    let removed = false;

    async function initializeMap() {
      const leafletModule = await import("leaflet");

      if (removed || !containerRef.current) {
        return;
      }

      const L = leafletModule.default;
      leafletRef.current = L;

      const map = L.map(containerRef.current, {
        zoomControl: false,
        attributionControl: true
      }).setView([fallbackCenter.latitude, fallbackCenter.longitude], 14);

      L.control.zoom({ position: "bottomright" }).addTo(map);

      L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
        maxZoom: 19,
        attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
      }).addTo(map);

      glowLineRef.current = L.polyline([], {
        color: "#ffffff",
        weight: 11,
        opacity: 0.95,
        lineCap: "round",
        lineJoin: "round"
      }).addTo(map);

      routeLineRef.current = L.polyline([], {
        color: "#00a4a8",
        weight: 6,
        opacity: 1,
        lineCap: "round",
        lineJoin: "round"
      }).addTo(map);

      mapRef.current = map;
      setMapReady(true);
    }

    initializeMap();

    return () => {
      removed = true;
      startMarkerRef.current?.remove();
      endMarkerRef.current?.remove();

      if (routeLineRef.current) {
        routeLineRef.current.remove();
        routeLineRef.current = null;
      }

      if (glowLineRef.current) {
        glowLineRef.current.remove();
        glowLineRef.current = null;
      }

      if (mapRef.current) {
        mapRef.current.remove();
        mapRef.current = null;
      }
    };
  }, [interactiveReady]);

  useEffect(() => {
    if (!interactiveReady || !mapRef.current || !leafletRef.current) {
      return;
    }

    const L = leafletRef.current;

    if (startMarkerRef.current) {
      startMarkerRef.current.remove();
      startMarkerRef.current = null;
    }

    if (endMarkerRef.current) {
      endMarkerRef.current.remove();
      endMarkerRef.current = null;
    }

    if (startLocation?.coordinates) {
      startMarkerRef.current = L.marker([startLocation.coordinates.latitude, startLocation.coordinates.longitude], {
        icon: L.divIcon(createMarkerIcon("start"))
      }).addTo(mapRef.current);
    }

    if (endLocation?.coordinates) {
      endMarkerRef.current = L.marker([endLocation.coordinates.latitude, endLocation.coordinates.longitude], {
        icon: L.divIcon(createMarkerIcon("end"))
      }).addTo(mapRef.current);
    }
  }, [endLocation?.coordinates, interactiveReady, mapReady, startLocation?.coordinates]);

  useEffect(() => {
    if (!interactiveReady || !mapRef.current || !routeLineRef.current || !glowLineRef.current) {
      return;
    }

    const latLngs = routeCoordinates.map(([longitude, latitude]) => [latitude, longitude]);
    routeLineRef.current.setLatLngs(latLngs);
    glowLineRef.current.setLatLngs(latLngs);

    const focusPoints = routeCoordinates.length > 1 ? routeCoordinates : selectedPoints;
    const bounds = getBounds(focusPoints);

    if (!bounds) {
      return;
    }

    if (bounds.west === bounds.east && bounds.south === bounds.north) {
      mapRef.current.flyTo([bounds.south, bounds.west], 15, {
        animate: true,
        duration: 0.6
      });
      return;
    }

    mapRef.current.fitBounds(
      [
        [bounds.south, bounds.west],
        [bounds.north, bounds.east]
      ],
      {
        // Keep the route clear of the trip sheet, which covers the left edge on wide screens.
        paddingTopLeft: [globalThis.innerWidth >= 900 ? 460 : 40, 48],
        paddingBottomRight: [48, 48],
        maxZoom: 16
      }
    );
  }, [interactiveReady, mapReady, routeCoordinates, selectedPoints]);

  if (!interactiveReady) {
    return (
      <section className="map-surface static" aria-label="Map preview">

      </section>
    );
  }

  return (
    <section className="map-surface">
      <div ref={containerRef} className="map-canvas" />
      {loading ? (
        <div className="map-status" aria-hidden="true">
          <span className="map-status-dot" />
          Finding route…
        </div>
      ) : null}
    </section>
  );
}

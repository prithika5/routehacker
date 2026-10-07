# Speed-limit prediction

Only 145 of the 1,644 roads in `data/davis.osm` have a `maxspeed` tag. Before this change, every other road was assumed to be 25 mph. This folder trains a scikit-learn Random Forest on OpenStreetMap road features to predict the missing speed limits. The C++ planner uses those predictions as a fallback when it computes route times.

```bash
pip install -r ml/requirements.txt
python3 ml/train.py        # prints the full diagnosis, writes the outputs below
make bin/routeplanner_web  # planner picks up data/speed_predictions.csv automatically
```

| Output | Purpose |
| --- | --- |
| `data/speed_predictions.csv` | `way_id,predicted_mph,confidence` for unlabeled roads (confidence >= 0.5). Read by the C++ planner. |
| `ml/metrics.json` | Every number reported below. |
| `ml/model.joblib` | Trained model (git-ignored; regenerate with `train.py`). |

## Features

Each road is described by its type (motorway, primary, ... residential), lanes, oneway, name and name suffix (Street, Boulevard, ...), `ref`, bridge/layer, surface, cycleway/bicycle/hgv/access tags, roundabout, traffic calming, segment length, node count, and centroid lat/lon. Tags that encode a speed directly (`maxspeed:hgv`, `maxspeed:trailer`, `maxspeed:type`, ...) are excluded to avoid label leakage.

## Class imbalance: diagnosis and fix

The labeled roads fall into these speed classes:

| Speed | Roads | Distinct streets |
| --- | --- | --- |
| 15 mph | 1 | 1 (Verona Terrace) |
| 25 mph | 38 | 18 |
| 30 mph | 43 | 4 |
| 35 mph | 17 | 1 (West Covell Blvd) |
| 55 mph | 1 | 1 (County Road 101A) |
| 65 mph | 45 | 31 |

**v1 (all six classes, unweighted)** reached 95.9% accuracy in 5-fold CV, but:
- it over-predicted 25 mph (44 predictions vs 38 true, precision 0.86): the singleton 15 and 55 mph roads and some 30 mph roads were absorbed into it;
- the 15 and 55 mph classes had 0% recall, since one example can't be both trained on and tested;
- it still emitted an unsupported 55 mph prediction on an unlabeled residential road.

**v2** removes classes with fewer than 5 examples (15 and 55 mph). That is the minimum needed for a class to appear in every stratified CV fold. v2 then retrains with `class_weight="balanced"`.

## Results (v2, classes 25/30/35/65 mph)

| Evaluation | Accuracy |
| --- | --- |
| Stratified 80/20 held-out test (29 roads) | **96.6%** (28/29; one 30 mph road predicted as 25) |
| Repeated stratified 5-fold CV (x10) | **97.2% ± 2.3%** |

Dropping the two singleton classes contributes to the higher v2 score. On the four retained classes, v1 and v2 score the same in the same CV (97.2%). Retraining improved reliability rather than raw accuracy: v2 never outputs a speed it has no support for.

## Integration

`include/PredictedSpeedStreetMap.h` wraps the street map. Any road without a real `maxspeed` tag that has a prediction exposes it as `maxspeed:predicted`. `DijkstraTransportationPlanner` resolves speed in this order: `maxspeed` tag, then `maxspeed:predicted`, then the configured default (25 mph). Road speed sets bus-edge travel time, so the predictions feed fastest-route time estimates. `bin/routeplanner_web --no-predicted-speeds` disables the fallback for A/B comparison.

Measured impact on four campus trips (fastest route):

| Trip | Without | With |
| --- | --- | --- |
| AggieWorks → West Village | 19 min | 18 min |
| Memorial Union → Research Park | 17 min | 17 min |
| Silo Terminal → Mondavi Center | 2 min | 2 min |
| Shields Library → West Village | 13 min | 13 min |

The effect is small because 83% of the unlabeled roads are residential, and the model (correctly) predicts 25 mph for most of them, which equals the old default.

Limitations: ramps (`*_link`) and paths have no labeled examples, so they are left on the default rather than extrapolated.

Tests: `make run_tptest` covers prediction loading, the fallback, and that real tags take priority.

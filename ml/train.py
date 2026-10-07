"""Train a Random Forest to predict missing OSM speed limits.

Pipeline
  1. Extract features for every highway way in data/davis.osm.
  2. v1 baseline: train on every speed class present in the data.
  3. Diagnose class imbalance (training distribution, per-class recall,
     prediction distribution on unlabeled roads).
  4. v2: drop classes without enough support to learn or evaluate, retrain
     with balanced class weights.
  5. Evaluate v2 on a stratified held-out split (+ repeated CV), then fit on all retained labels and write
     predictions for roads with no maxspeed tag.

Outputs
  data/speed_predictions.csv  way_id,predicted_mph,confidence   (read by C++)
  ml/model.joblib             trained v2 model
  ml/metrics.json             all numbers reported below

Usage:  python3 ml/train.py [--osm data/davis.osm]
"""

import argparse
import json
import os
import sys
from collections import Counter

import joblib
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.model_selection import (
    KFold,
    RepeatedStratifiedKFold,
    cross_val_predict,
    cross_val_score,
    train_test_split,
)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from features import build_frame, feature_columns  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEED = 42
MIN_CLASS_SUPPORT = 5  # need >= 5 examples for a class to appear in every CV fold
TEST_SIZE = 0.2
# Road classes the model has labeled examples for. Ramps (*_link) and paths
# have no labels, so the model is not asked to extrapolate to them.
PREDICT_HIGHWAYS = {"motorway", "primary", "secondary", "tertiary", "residential"}
MIN_CONFIDENCE = 0.5


def make_model(class_weight=None):
    return RandomForestClassifier(
        n_estimators=300,
        min_samples_leaf=1,
        class_weight=class_weight,
        random_state=SEED,
        n_jobs=-1,
    )


def dist(values):
    c = Counter(int(v) for v in values)
    return {k: c[k] for k in sorted(c)}


def section(title):
    print(f"\n{'=' * 70}\n{title}\n{'=' * 70}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--osm", default=os.path.join(ROOT, "data", "davis.osm"))
    ap.add_argument("--out", default=os.path.join(ROOT, "data", "speed_predictions.csv"))
    args = ap.parse_args()

    df = build_frame(args.osm)
    cols = feature_columns(df)
    labeled = df[df.speed_mph.notna()].copy()
    labeled["speed_mph"] = labeled.speed_mph.astype(int)
    unlabeled = df[df.speed_mph.isna() & df.highway.isin(PREDICT_HIGHWAYS)].copy()
    metrics = {"n_ways": int(len(df)), "n_labeled": int(len(labeled)),
               "n_unlabeled_target": int(len(unlabeled)), "features": cols}

    # ------------------------------------------------------------------ v1
    section("1. Training distribution (all labeled ways)")
    support = labeled.speed_mph.value_counts().sort_index()
    streets = labeled.groupby("speed_mph").name.nunique()
    for s in support.index:
        print(f"  {s:>3} mph: {support[s]:>3} ways  ({streets[s]} distinct streets)")
    metrics["class_support"] = {int(k): int(v) for k, v in support.items()}
    metrics["class_distinct_streets"] = {int(k): int(v) for k, v in streets.items()}

    section("2. v1 baseline: all classes, unweighted")
    X, y = labeled[cols], labeled.speed_mph
    # Singleton classes make stratification impossible, so v1 uses shuffled KFold.
    v1_pred = cross_val_predict(make_model(), X, y, cv=KFold(5, shuffle=True, random_state=SEED))
    v1_acc = accuracy_score(y, v1_pred)
    print(f"  5-fold CV accuracy: {v1_acc:.3f}")
    print(classification_report(y, v1_pred, zero_division=0))
    v1_full = make_model().fit(X, y)
    v1_unl = v1_full.predict(unlabeled[cols])
    print(f"  CV prediction distribution:        {dist(v1_pred)}")
    print(f"  true label distribution:           {dist(y)}")
    print(f"  predictions on unlabeled roads:    {dist(v1_unl)}")
    v1_report = classification_report(y, v1_pred, zero_division=0, output_dict=True)
    metrics["v1"] = {
        "cv_accuracy": v1_acc,
        "cv_pred_distribution": dist(v1_pred),
        "unlabeled_pred_distribution": dist(v1_unl),
        "per_class_recall": {k: v["recall"] for k, v in v1_report.items() if k.isdigit()},
    }

    # ------------------------------------------------------------------ diagnosis
    section("3. Diagnosis")
    dropped = sorted(int(s) for s in support.index if support[s] < MIN_CLASS_SUPPORT)
    kept = sorted(int(s) for s in support.index if support[s] >= MIN_CLASS_SUPPORT)
    print(f"  Classes with < {MIN_CLASS_SUPPORT} examples (cannot be learned or evaluated): {dropped}")
    for s in dropped:
        rows = labeled[labeled.speed_mph == s]
        for _, r in rows.iterrows():
            print(f"    {s} mph <- {r['name']} ({r.highway})")
    pred_c, true_c = Counter(int(v) for v in v1_pred), Counter(int(v) for v in y)
    over = max(pred_c, key=lambda k: pred_c[k] - true_c[k])
    print(f"  v1 over-predicts {over} mph in CV: {pred_c[over]} predictions vs {true_c[over]} true "
          f"(precision {v1_report[str(over)]['precision']:.2f}); minority-class roads are absorbed "
          f"into it, and the singleton classes get 0 recall.")
    leaked = {k: v for k, v in dist(v1_unl).items() if k in dropped}
    if leaked:
        print(f"  v1 still emits unsupported classes on unlabeled roads: {leaked}")
    metrics["dropped_classes"] = dropped
    metrics["kept_classes"] = kept

    # ------------------------------------------------------------------ v2
    section(f"4. v2: classes {kept}, balanced class weights")
    kept_df = labeled[labeled.speed_mph.isin(kept)]
    X2, y2 = kept_df[cols], kept_df.speed_mph

    X_tr, X_te, y_tr, y_te = train_test_split(
        X2, y2, test_size=TEST_SIZE, stratify=y2, random_state=SEED)
    v2 = make_model("balanced").fit(X_tr, y_tr)
    te_pred = v2.predict(X_te)
    holdout_acc = accuracy_score(y_te, te_pred)
    print(f"  Held-out test accuracy ({len(y_te)} ways, stratified {TEST_SIZE:.0%}): {holdout_acc:.3f}")
    print(classification_report(y_te, te_pred, zero_division=0))
    print("  Confusion matrix (rows=true, cols=pred, labels=%s):" % kept)
    print(confusion_matrix(y_te, te_pred, labels=kept))

    rcv = cross_val_score(make_model("balanced"), X2, y2,
                          cv=RepeatedStratifiedKFold(n_splits=5, n_repeats=10, random_state=SEED))
    print(f"\n  Repeated stratified 5-fold CV (x10): {rcv.mean():.3f} +/- {rcv.std():.3f}")


    # Same protocol as v1 for an apples-to-apples comparison on the kept classes.
    v1_on_kept = accuracy_score(y2, cross_val_predict(
        make_model(), X2, y2, cv=KFold(5, shuffle=True, random_state=SEED)))
    v2_same_cv = accuracy_score(y2, cross_val_predict(
        make_model("balanced"), X2, y2, cv=KFold(5, shuffle=True, random_state=SEED)))

    final = make_model("balanced").fit(X2, y2)
    proba = final.predict_proba(unlabeled[cols])
    pred = final.classes_[proba.argmax(axis=1)]
    conf = proba.max(axis=1)
    print(f"\n  v2 predictions on unlabeled roads: {dist(pred)}")
    print(f"  confidence >= {MIN_CONFIDENCE}: {(conf >= MIN_CONFIDENCE).sum()}/{len(conf)}")

    importances = sorted(zip(cols, final.feature_importances_), key=lambda t: -t[1])[:10]
    print("  Top features:", ", ".join(f"{c}={v:.2f}" for c, v in importances))

    rep = classification_report(y_te, te_pred, zero_division=0, output_dict=True)
    metrics["v2"] = {
        "holdout_accuracy": holdout_acc,
        "holdout_size": int(len(y_te)),
        "holdout_per_class": {k: {"precision": v["precision"], "recall": v["recall"],
                                  "support": int(v["support"])}
                              for k, v in rep.items() if k.isdigit()},
        "holdout_confusion": confusion_matrix(y_te, te_pred, labels=kept).tolist(),
        "repeated_cv_mean": float(rcv.mean()),
        "repeated_cv_std": float(rcv.std()),
        "kfold_cv_accuracy": v2_same_cv,
        "v1_kfold_cv_accuracy_on_kept_classes": v1_on_kept,
        "unlabeled_pred_distribution": dist(pred),
        "top_features": {c: float(v) for c, v in importances},
    }

    # ------------------------------------------------------------------ outputs
    out = unlabeled.assign(predicted_mph=pred, confidence=conf)
    out = out[out.confidence >= MIN_CONFIDENCE]
    out[["id", "predicted_mph", "confidence"]].rename(columns={"id": "way_id"}).to_csv(
        args.out, index=False, float_format="%.3f")
    joblib.dump({"model": final, "features": cols, "classes": kept},
                os.path.join(ROOT, "ml", "model.joblib"))
    metrics["n_predictions_written"] = int(len(out))
    with open(os.path.join(ROOT, "ml", "metrics.json"), "w") as f:
        json.dump(metrics, f, indent=2, default=float)

    section("5. Outputs")
    print(f"  {len(out)} predictions -> {os.path.relpath(args.out, ROOT)}")
    print("  model -> ml/model.joblib, metrics -> ml/metrics.json")


if __name__ == "__main__":
    main()

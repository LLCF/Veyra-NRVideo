"""Compare identical 50 ms snapshot probes; these are not every-frame traces."""
import argparse
import csv
import json
from pathlib import Path
from statistics import mean, median


def percentile(values, p):
    values = sorted(values)
    position = (len(values) - 1) * p
    lo = int(position)
    return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (position - lo)


def analyze(directory):
    with (directory / "snapshots.csv").open(encoding="utf-8") as stream:
        samples = [{key: float(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    if not samples:
        raise ValueError(f"No samples: {directory}")
    # Keep only snapshots with a new real Present counter. This avoids weighting
    # a stalled/stale snapshot as if the same frame had been presented again.
    rows = []
    previous = None
    for row in samples:
        if row["realPresented"] != previous:
            rows.append(row)
        previous = row["realPresented"]
    result = {"snapshots": len(samples), "distinctRealPresentSnapshots": len(rows),
              "secondsBetweenSnapshots": samples[-1]["wall"] - samples[0]["wall"],
              "continuouslyActive4X": all(r["nr"] == 1 and r["fg"] == 1 and r["multiplier"] == 4 for r in samples),
              "limitedSnapshotPercent": 100 * mean(r["limited"] for r in samples)}
    result["deltas"] = {key: samples[-1][key] - samples[0][key] for key in
                        ("received", "dropped", "generated", "realPresented", "generatedPresented", "fgSkipped", "fgExpired", "slotWaits")}
    values = [r["ageMs"] for r in rows]
    result["callbackToRealPresentReturnMs"] = {
        "p50": median(values), "p95": percentile(values, .95), "p99": percentile(values, .99), "max": max(values),
        "first30sMedian": median(r["ageMs"] for r in rows if r["wall"] < 30),
        "last30sMedian": median(r["ageMs"] for r in rows if r["wall"] > samples[-1]["wall"] - 30)}
    result["medianSnapshotMetrics"] = {key: median(r[key] for r in rows) for key in
        ("callbackFps", "submitFps", "readAgeMs", "readyMeanMs", "deadlineMeanMs", "presentMeanMs",
         "colorMeanMs", "flowMeanMs", "nrMeanMs", "residualMeanMs", "fgMeanMs", "graphMeanMs")}
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("names", nargs="+")
    args = parser.parse_args()
    results = {name: analyze(args.root / name) for name in args.names}
    text = json.dumps(results, indent=2)
    (args.root / "comparison.json").write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()

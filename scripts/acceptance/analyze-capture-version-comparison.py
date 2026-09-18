"""Compare identical 50 ms snapshot probes; these are not every-frame traces."""
import argparse
import csv
import json
import re
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
    multipliers = sorted({int(r["multiplier"]) for r in samples})
    result["multipliers"] = multipliers
    result["continuouslyActive"] = len(multipliers) == 1 and all(r["nr"] == 1 and r["fg"] == 1 for r in samples)
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
    log_path = directory / "engine.log"
    if log_path.exists():
        # Whole-run events include startup/shutdown, unlike the CSV window.
        # Some device names use the Windows ANSI code page; event keys are ASCII.
        log_text = log_path.read_text(encoding="utf-8-sig", errors="replace")
        result["logDecodeReplacementCount"] = log_text.count("\ufffd")
        lines = log_text.splitlines()
        result["wholeRunSlowEvents"] = [line for line in lines if "slow=true" in line or re.search(r"\[[\w-]*stall\]", line)]
        result["wholeRunErrors"] = [line for line in lines if "[ERROR" in line]
        result["allocatorNegotiation"] = [line for line in lines if "[capture-buffer]" in line]
        measuring = False
        originals = []
        invalid_ready = 0
        for line in lines:
            if "[capture-comparison] measurement-start" in line:
                measuring = True
            elif "[capture-comparison] measurement-end" in line:
                measuring = False
            elif measuring and "[capture-present-sample]" in line and "generated=false" in line:
                fields = {k: int(v) for k, v in re.findall(r"(arrival|ready|begin|end)=([0-9]+)", line)}
                if len(fields) != 4 or not 0 < fields["arrival"] <= fields["ready"] <= fields["begin"] <= fields["end"]:
                    invalid_ready += 1
                    continue
                originals.append({
                    "callbackToObservedReadyMs": (fields["ready"] - fields["arrival"]) / 10000,
                    "observedReadyToPresentBeginMs": (fields["begin"] - fields["ready"]) / 10000,
                    "presentCallMs": (fields["end"] - fields["begin"]) / 10000,
                    "callbackToPresentEndMs": (fields["end"] - fields["arrival"]) / 10000})
        if originals or invalid_ready:
            result["originalFrameTrace"] = {"count": len(originals), "invalidReadyCount": invalid_ready}
            for key in originals[0] if originals else []:
                values = [row[key] for row in originals]
                result["originalFrameTrace"][key] = {"p50": median(values), "p95": percentile(values, .95), "max": max(values)}
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

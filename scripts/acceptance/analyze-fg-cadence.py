"""Inspect verbose capture timing; all intervals are software observations."""
import argparse
import json
import math
import pathlib
import re
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    result = {"count": len(values), "mean": statistics.mean(values)}
    for name, q in (("p50", .5), ("p95", .95), ("p99", .99), ("max", 1)):
        result[name] = values[max(0, math.ceil(len(values) * q) - 1)]
    return {k: round(v, 4) for k, v in result.items()}


def analyze(path):
    groups = {k: [] for k in ("pacing-sample", "capture-graph-sample", "capture-present-sample")}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        for tag, rows in groups.items():
            marker = "[" + tag + "]"
            if marker in line:
                fields = dict(re.findall(r"(\w+)=([^ ]+)", line.split(marker, 1)[1]))
                rows.append(fields)
                break
    presentations = groups["pacing-sample"]
    if len(presentations) < 2:
        raise ValueError("No per-frame presentation samples")
    threshold = int(presentations[0]["end"]) + 100_000_000
    presentations = [p for p in presentations if int(p["end"]) >= threshold]
    if len(presentations) < 2:
        raise ValueError("Less than 10 seconds of observations")
    graphs = [g for g in groups["capture-graph-sample"] if int(g["start"]) >= threshold]
    captured = [p for p in groups["capture-present-sample"] if int(p["end"]) >= threshold]
    gaps = []
    for a, b in zip(presentations, presentations[1:]):
        start, end = int(a["end"]), int(b["begin"])
        overlaps = [(g, max(0, min(end, int(g["submitted"])) - max(start, int(g["start"])))) for g in graphs]
        matching = [(g, ticks) for g, ticks in overlaps if ticks > 0]
        gaps.append({"intervalMs": (int(b["end"]) - start) / 10000,
                     "idleMs": (end - start) / 10000,
                     "graphOverlapMs": sum(ticks for _, ticks in matching) / 10000,
                     "graphSources": [int(g["source"]) for g, _ in matching],
                     "nextPts": int(b["pts"]), "nextGenerated": b["generated"],
                     "begin": start, "end": int(b["end"])})
    intervals = [g["intervalMs"] for g in gaps]
    groups_by_batch = {}
    for p in captured:
        groups_by_batch.setdefault(p["batch"], []).append(p)
    long = [g for g in gaps if g["intervalMs"] > 16.667]
    return {
        "scope": "CPU software submission times; no scanout or unique-pixel measurement; first 10s excluded",
        "samples": len(presentations),
        "submitFps": (len(presentations) - 1) * 1e7 / (int(presentations[-1]["end"]) - int(presentations[0]["end"])),
        "intervalMs": distribution(intervals),
        "intervalUnder1ms": sum(x < 1 for x in intervals),
        "intervalOver16_667ms": len(long),
        "intervalOver25ms": sum(x > 25 for x in intervals),
        "intervalOver33_333ms": sum(x > 33.333 for x in intervals),
        "longGapGraphOverlapFraction": sum(g["graphOverlapMs"] for g in long) / sum(g["idleMs"] for g in long) if long else 0,
        "presentCpuMs": distribution([(int(p["end"]) - int(p["begin"])) / 10000 for p in presentations]),
        "lateMs": distribution([float(p["lateMs"]) for p in presentations]),
        "mediaStepMs": distribution([(int(b["pts"]) - int(a["pts"])) / 10000 for a, b in zip(presentations, presentations[1:])]),
        "graphSubmitMs": distribution([(int(g["submitted"]) - int(g["start"])) / 10000 for g in graphs]),
        "graphSlotWaitMs": distribution([float(g["slotWaitMs"]) for g in graphs]),
        "batchPresentCounts": {str(n): sum(len(v) == n for v in groups_by_batch.values()) for n in range(1, 7)},
        "longestGaps": sorted(gaps, key=lambda g: g["intervalMs"], reverse=True)[:12],
    }


def analyze_trace(path):
    events = [dict(re.findall(r"(\w+)=([^ ]+)", line)) for line in
              path.read_text(encoding="utf-8", errors="replace").splitlines() if line.startswith("event=")]
    presents = [e for e in events if e["event"] == "Present"]
    if len(presents) < 2:
        raise ValueError("No retained presentation events")
    intervals = [(int(b["host"]) - int(a["host"])) / 10000 for a, b in zip(presents, presents[1:])]
    submits = {e["batch"]: e for e in events if e["event"] == "Submitted"}
    batches = {}
    for p in presents:
        batches.setdefault(p["batch"], []).append(p)
    # Discard partial batches at both retained-window boundaries.
    complete = list(batches)[1:-1]
    counts = {str(n): sum(len(batches[b]) == n for b in complete) for n in range(1, 7)}
    only_real = [b for b in complete if len(batches[b]) == 1 and batches[b][0]["detail"] == "0"]
    rejected = sum(b in submits and int(submits[b]["detail"]) > 0 for b in only_real)
    warmup = sum(b in submits and submits[b]["count"] == "1" for b in only_real)
    def state(batch):
        submit = submits.get(batch)
        if submit is None:
            return "unknown"
        if int(submit["detail"]) > 0:
            return "rejected"
        return "warmup" if batch in only_real and submit["count"] == "1" else "steady"
    transitions = {}
    for a, b in zip(complete, complete[1:]):
        key = state(a) + "->" + state(b)
        transitions[key] = transitions.get(key, 0) + 1
    span = (int(presents[-1]["host"]) - int(presents[0]["host"])) / 1e7
    ready = {(e["batch"], e["detail"]): e for e in events if e["event"] == "FrameReady"}
    discarded = [e for e in events if e["event"] == "Discarded"]
    observed_delays = [(int(e["host"]) - int(ready[(e["batch"], e["detail"])]["host"])) / 10000
                       for e in discarded if (e["batch"], e["detail"]) in ready]
    discard_summary = {
        "count": len(discarded),
        "withRetainedReadyObservation": len(observed_delays),
        "readyObservedBeforeDiscard": sum(v >= 0 for v in observed_delays),
        "readyObservedAfterDiscard": sum(v < 0 for v in observed_delays),
        "observedReadyToDiscardMs": distribution(observed_delays) if observed_delays else None,
        "latenessMs": distribution([float(e["ms"]) for e in discarded]) if discarded else None,
        "note": "CPU fence observations are upper bounds on GPU completion time; absent observations are unknown",
    }
    return {"scope": "Bounded memory trace, final retained window only; software submission, not scanout",
            "seconds": span, "samples": len(presents), "submitFps": (len(presents) - 1) / span,
            "intervalMs": distribution(intervals),
            "intervalUnder1ms": sum(v < 1 for v in intervals),
            "intervalOver16_667ms": sum(v > 16.667 for v in intervals),
            "intervalOver25ms": sum(v > 25 for v in intervals),
            "intervalOver33_333ms": sum(v > 33.333 for v in intervals),
            "batchPresentCounts": counts, "onlyRealBatches": len(only_real),
            "onlyRealRejectedBatches": rejected, "onlyRealWarmupBatches": warmup,
            "batchTransitions": transitions,
            "discardedSubframes": discard_summary,
            "presentCpuMs": distribution([float(p["ms"]) for p in presents]),
            "mediaStepMs": distribution([(int(b["pts"]) - int(a["pts"])) / 10000 for a, b in zip(presents, presents[1:])])}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--trace", action="store_true")
    args = parser.parse_args()
    result = analyze_trace(args.log) if args.trace else analyze(args.log)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "longestGaps"}, indent=2))

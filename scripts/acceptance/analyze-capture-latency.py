"""Join physical capture traces without mixing CPU and GPU clocks."""
import argparse
import csv
import json
import re
from collections import defaultdict
from pathlib import Path


def stats(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    def percentile(p):
        i = (len(values) - 1) * p
        lo = int(i)
        return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (i - lo)
    return {"n": len(values), "mean": sum(values) / len(values),
            "p50": percentile(.5), "p95": percentile(.95),
            "p99": percentile(.99), "max": values[-1]}


def analyze(directory):
    result = (directory / "result.txt").read_text(encoding="utf-8-sig")
    start = int(re.search(r"measurementStart=(\d+)", result)[1])
    end = int(re.search(r"measurementEnd=(\d+)", result)[1])
    seconds = (end - start) / 10_000_000
    events = defaultdict(list)
    pattern = re.compile(r"\[(capture-(?:ingress|read|graph|present)-sample|gpu-timestamp)\] (.*)")
    for line in (directory / "engine.log").open(encoding="utf-8-sig", errors="replace"):
        match = pattern.search(line)
        if not match:
            continue
        fields = {}
        for key, value in re.findall(r"(\w+)=([^\s]+)", match[2]):
            if value in ("true", "false"):
                fields[key] = value == "true"
            else:
                fields[key] = float(value) if "." in value else int(value)
        events[match[1]].append(fields)
    ingress = {(x["source"], x["arrival"]): x for x in events["capture-ingress-sample"]}
    reads = {(x["source"], x["arrival"]): x for x in events["capture-read-sample"]}
    graphs = {(x["source"], x["epoch"], x["revision"]): x for x in events["capture-graph-sample"]}
    measured_graphs = {k: x for k, x in graphs.items() if start <= x["arrival"] < end}
    values = defaultdict(list)
    unmatched = defaultdict(int)
    def ms(name, a, b):
        if b < a:
            raise ValueError(f"negative duration: {name}: {a}, {b}")
        values[name].append((b - a) / 10_000)
    arrivals = sorted(x["arrival"] for x in ingress.values() if start <= x["arrival"] < end)
    for a, b in zip(arrivals, arrivals[1:]):
        ms("callback_interval", a, b)
    for x in ingress.values():
        if start <= x["arrival"] < end:
            ms("callback_copy_lock", x["arrival"], x["copied"])
    for x in measured_graphs.values():
        key = (x["source"], x["arrival"])
        r, i = reads.get(key), ingress.get(key)
        if r and i:
            ms("mailbox_until_read", i["copied"], r["read"])
            ms("read_to_graph_start", r["read"], x["start"])
        else:
            unmatched["graph_ingress_read"] += 1
        ms("graph_cpu_submission", x["start"], x["submitted"])
        values["graph_slot_wait"].append(x["slotWaitMs"])
    presented = [x for x in events["capture-present-sample"] if start <= x["end"] < end]
    rows = []
    for x in presented:
        kind = "generated" if x["generated"] else "real"
        ms(kind + "_callback_to_present_return", x["arrival"], x["end"])
        ms(kind + "_present_call", x["begin"], x["end"])
        if x["ready"]:
            ms(kind + "_callback_to_ready_observed", x["arrival"], x["ready"])
            ms(kind + "_ready_to_present_begin", x["ready"], x["begin"])
        if x["generated"] and x["arrivalA"]:
            ms("generated_a_callback_to_present_return", x["arrivalA"], x["end"])
            ms("generated_ab_arrival_gap", x["arrivalA"], x["arrival"])
        g = graphs.get((x["source"], x["epoch"], x["revision"]))
        if g and g["arrival"] == x["arrival"] and x["ready"]:
            ms(kind + "_submit_to_ready_observed", g["submitted"], x["ready"])
        else:
            unmatched["missing_ready" if not x["ready"] else "present_graph"] += 1
        if not x["generated"]:
            for name, lo, hi in [("real_first_30s", start, start + 300_000_000),
                                 ("real_last_30s", end - 300_000_000, end)]:
                if lo <= x["end"] < hi:
                    ms(name, x["arrival"], x["end"])
        rows.append({**x, "softwareLatencyMs": (x["end"] - x["arrival"]) / 10_000})
    for a, b in zip(presented, presented[1:]):
        ms("present_interval", a["end"], b["end"])
    gpu_groups = defaultdict(list)
    stages = {0: "upload_color", 1: "sr", 2: "flow", 3: "nr", 4: "residual",
              5: "fg1", 6: "fg2", 7: "fg3", 8: "fg4", 9: "fg5",
              10: "fg_batch", 11: "blit", 12: "hdr"}
    for x in events["gpu-timestamp"]:
        key = (x["frame"], x["epoch"], x["revision"])
        if key in measured_graphs:
            duration = (x["end"] - x["begin"]) / x["frequency"] * 1000
            values["gpu_" + stages[x["stage"]]].append(duration)
            if x["stage"] != 11:
                gpu_groups[key].append(x)
    for group in gpu_groups.values():
        frequencies = {x["frequency"] for x in group}
        if len(frequencies) != 1:
            raise ValueError("GPU clock frequency mismatch")
        values["gpu_graph_envelope"].append(
            (max(x["end"] for x in group) - min(x["begin"] for x in group)) / group[0]["frequency"] * 1000)
    snapshots = list(csv.DictReader((directory / "capture-snapshots.csv").open()))
    snapshots = [x for x in snapshots if start <= int(x["host"]) < end]
    first, last = snapshots[0], snapshots[-1]
    counters = {key: int(last[key]) - int(first[key]) for key in
                ["received", "dropped", "processed", "generated", "realPresented", "generatedPresented", "fgSkipped", "fgExpired"]}
    counters.update(limitedFraction=sum(int(x["limited"]) for x in snapshots) / len(snapshots),
                    nrAllActive=all(int(x["nr"]) for x in snapshots),
                    fgAllActive=all(int(x["fg"]) for x in snapshots),
                    effectiveModes=sorted({int(x["effective"]) for x in snapshots}),
                    callbackEvents=len(arrivals), presentEvents=len(presented),
                    callbackFps=len(arrivals) / seconds, presentSubmitFps=len(presented) / seconds)
    formats = sorted({(x["width"], x["height"], x["format"]) for x in reads.values()
                      if start <= x["arrival"] < end})
    assert formats == [(3840, 2160, 23)], f"Unexpected input formats: {formats}"
    counters["observedInputFormats"] = formats
    with (directory / "joined-presents.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    return {"name": directory.name, "seconds": seconds, "result": result,
            "counters": counters, "unmatched": dict(unmatched),
            "milliseconds": {k: stats(v) for k, v in values.items()}}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--names", nargs="+", default=["low", "even", "reflex", "off"])
    args = parser.parse_args()
    results = [analyze(args.root / name) for name in args.names]
    (args.root / "comparison.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    lines = ["| Metric (ms, P50 / P95) | " + " | ".join(x["name"] for x in results) + " |",
             "| --- | " + " | ".join("---:" for x in results) + " |"]
    for key in results[0]["milliseconds"]:
        cells = []
        for result in results:
            s = result["milliseconds"].get(key)
            cells.append(f"{s['p50']:.3f} / {s['p95']:.3f}" if s else "unmeasured")
        lines.append("| " + key + " | " + " | ".join(cells) + " |")
    (args.root / "stages.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    with (args.root / "comparison.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(["mode", "metric", "n", "mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms"])
        for result in results:
            for key, s in result["milliseconds"].items():
                writer.writerow([result["name"], key, *[s[x] for x in ["n", "mean", "p50", "p95", "p99", "max"]]])
            print(result["name"], json.dumps(result["counters"]))
            print("real latency:", result["milliseconds"]["real_callback_to_present_return"])


if __name__ == "__main__":
    main()

"""Summarize CPU observations, never physical display latency."""
import csv
import json
import pathlib
import re
import statistics
import sys

root = pathlib.Path(sys.argv[1]).resolve()
rows = []


def percentile(values, fraction):
    values = sorted(values)
    return round(values[min(len(values)-1, int((len(values)-1)*fraction))], 3) if values else None


for directory in sorted(root.iterdir()):
    log = directory / 'engine.log'
    result = directory / 'result.txt'
    if not log.is_file() or not result.is_file() or 'PASS clean stop' not in result.read_text(encoding='utf-8', errors='replace'):
        continue
    if 'FAIL ' in result.read_text(encoding='utf-8',errors='replace'):
        continue
    samples = []
    expired_generated = 0
    for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
        expired = re.search(r'\bexpiredGenerated=(\d+)', line)
        if expired:
            expired_generated = int(expired.group(1))
        if '[pacing-sample]' in line:
            fields = dict(re.findall(r'(\w+)=([^ ]+)', line.split('[pacing-sample]', 1)[1]))
            samples.append(fields)
    if not samples:
        continue
    # Same 2-second warmup exclusion for every case, relative to first submission.
    threshold = int(samples[0]['end']) + 20_000_000
    samples = [s for s in samples if int(s['end']) >= threshold]
    if len(samples) < 2:
        continue
    ends = [int(s['end']) for s in samples]
    intervals = [(b-a)/10000 for a, b in zip(ends, ends[1:])]
    process = [(int(s['end'])-int(s['process']))/10000 for s in samples]
    ready = [(int(s['end'])-int(s['ready']))/10000 for s in samples if int(s['ready']) > 0]
    submit = [(int(s['end'])-int(s['begin']))/10000 for s in samples]
    snapshots = list(csv.DictReader((directory/'snapshots.csv').open(encoding='utf-8')))
    speed = ((float(snapshots[-1]['position'])-float(snapshots[0]['position'])) /
             (float(snapshots[-1]['wall'])-float(snapshots[0]['wall']))) if len(snapshots)>1 else 0
    row = dict(case=directory.name, samples=len(samples), generated=sum(s['generated']=='true' for s in samples),
               submitFps=round((len(ends)-1)*1e7/(ends[-1]-ends[0]), 2), mediaSpeed=round(speed, 3),
               intervalP50=percentile(intervals,.5), intervalP95=percentile(intervals,.95),
               intervalP99=percentile(intervals,.99), intervalMax=round(max(intervals),3),
               processToReturnP50=percentile(process,.5), processToReturnP95=percentile(process,.95),
               readyToReturnP50=percentile(ready,.5), readyToReturnP95=percentile(ready,.95),
               presentCpuP95=percentile(submit,.95), queueMax=max(int(s['queue']) for s in samples),
               finalSkipped=int(snapshots[-1]['skipped']), expiredGenerated=expired_generated)
    rows.append(row)
if not rows:
    raise SystemExit('No measurements')
with (root/'summary.csv').open('w', newline='', encoding='utf-8') as output:
    writer = csv.DictWriter(output, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
(root/'summary.json').write_text(json.dumps({'units':'ms; software observations only; display unmeasured', 'cases':rows},indent=2), encoding='utf-8')
for row in rows:
    print(json.dumps(row))

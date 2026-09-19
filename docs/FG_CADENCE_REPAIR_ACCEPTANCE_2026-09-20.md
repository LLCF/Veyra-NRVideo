# FG cadence: partial acceptance, fixed 6X still open

## Scope and result

Branch: `codex/fg-cadence-audit-20260920`. RTX5070, user p001.mp4,
3840x2160 60fps, NR realtime 1080, 4K output target, DLSS 6X/2X,
pacing setting off. The source already matches the 4K target, so SR does
not execute. No package, push or release. No physical scanout measurement.

The requested NR-on fixed 6X uniform cadence has **not passed**. Promoted
prefix admission and pacing reduce the original roughly 80 submissions/s
collapse to about 265, but repeated admission rejection and history warmup
still create 15-17ms holes. Automatic 4X fallback is separately successful;
it must not be used as evidence that fixed 6X was solved.

## Changes under test

- File DLSS admission checks first-output cost against its deadline and whole
  group cost against the last deadline. Account for older queued GPU work,
  retaining predicted start when an earlier completion watch retires.
- Normal file DLSS playback enforces the existing 90% minimum output spacing
  after the audio-clock deadline, preventing dense catch-up submissions.
  XeSS/FSR provider pacing and capture admission are not changed by this rule.
- Sixteen command allocators cover two bounded jobs, each with up to three
  enhancement lists and five MFG lists. The two-job scheduler and texture
  history capacities remain unchanged. This is a deliberate expansion from
  the earlier six-slot allocator budget, not a sixteen-frame queue.
- Logs distinguish requested/effective multipliers, first deadline and queued
  GPU cost. Failed admission/spacing experiment switches were removed.
- Earlier checkpoint `1be28a4` preserves valid prior generated leases across
  soft drops, while still resetting processing history and invalidating on
  hard boundaries. Earlier fallback checkpoint: `0050870`.

## Measured results

Each trace is the **final retained window** of a bounded in-memory trace,
not the entire run. FPS below means application submissions, not display.
All GPU runs were sequential; startup, window size and media were matched.

| Run | Duration | Retained seconds | FPS | P95 / max gap ms | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| prefix2, NR on 2X | 30s | 11.367 | 120.001 | 8.751 / 9.061 | throughput assertion >=95% passed |
| prefix6, NR on fixed 6X | 45s | 5.760 | 265.445 | 15.428 / 17.169 | lifecycle only; cadence fails |
| prefix-adaptive6, NR on request 6X, actual 4X | 30s | 6.204 | 240.007 | 4.514 / 5.560 | fallback only |
| prefix-headroom6, effects off fixed 6X | 30s | 4.869 | 360.004 | 3.198 / 3.592 | diagnostic; throughput assertion >=95% passed |

No sub-ms gaps in these retained windows. prefix6 retains 236 complete
six-frame batches, 54 rejected pairs and 54 warmups, with no generated
discards in that window (16 over the whole run). Its 19 gaps over16.667ms
are why the higher average FPS is insufficient for acceptance.

The NR-on serial stage measurements are approximately NR6.8ms, flow1.2ms,
FG9.8-10ms, plus remaining work, against a 16.667ms source interval.
This establishes an over-budget **current serial implementation**; it does
not prove the hardware cannot benefit from a different implementation.
Removing admission entirely previously caused ~59fps and 1620 expired
generated frames in the retained trace. More buffering cannot cure a
sustained service-time deficit without growing latency.

XeSS `prefix-xess4`: 30s, lifecycle passes, app60/s and SDK240/s in the
final reported samples, zero admission skips/expiration. SDK counters do
not establish subframe scanout timing. Earlier XeSS2X evidence remains in
`xess-final2`; no claim of a new 2X run after the allocator change.

CPU policy suite: 121 PASS, exit0, including first/last deadlines, queued
cost, CPU time not erasing future FG work and history warmup. GPU
`prefix-content6`: dynamic2/4/6/default, real PTS, reset/recovery,40 NR
frames,144 FG evaluations,140 valid outputs, debugErrors=0, exit0.

`prefix-lifecycle`: paused seek, resume, live pacing-mode changes, resize,
off-mode playback and clean stop pass, exit0. `prefix-smoke`: main program
`--smoke-empty --smoke-seconds 5`, exit0. These validate lifecycle, not
NR-on fixed6 cadence. The final rebuild only added diagnostic log fields
after prefix2/6/adaptive6/headroom6; scheduling behavior is identical.

## Reproduction and evidence

Outputs: `E:/项目/Veyra/tests/fg-cadence-repair-20260920/<run>` and
`<run>.{stdout,stderr}.log`, JSON from `scripts/acceptance/analyze-fg-cadence.py`.
Build/log root: `E:/项目/Veyra/logs/fg-cadence-repair-20260920`.
Temporary root: `E:/项目/Veyra/tmp/fg-cadence-repair-20260920`.
Current local build: `E:/项目/Veyra/build/slider-reset-20260919/veyra.exe`.

Build via `scripts/build-isolated.ps1`, existing build/cache, display1.4.3,
targets `veyra`, `veyra_fg_sustained_tests`, `veyra_presentation_worker_tests`,
`veyra_fg_admission_tests`, `veyra_presentation_pacing_tests`; logs
`prefix-final-build.log`, `prefix-lifecycle-build.log`, `prefix-final-cpu.log`.
Run via `scripts/run-short-test.ps1`,95s watchdog (admission150s), working
directory `E:/项目/Veyra/tests/dlss-recovery-20260920`, process TEMP/TMP set
to the temporary root. Sustained arguments: media,output,multiplier,on,
seconds,on,minimumRatio,file-4k; headroom uses effects off, XeSS uses
file-xess-4k. Fixed6 diagnostics set `VEYRA_TEST_FIXED_FG_MULTIPLIER=1`;
per-subframe analysis sets `VEYRA_TEST_TRACE_SUBFRAMES=1`.

## Remaining acceptance

NR-on fixed6X smoothness remains open. Investigate serial enhancement/FG
cost and reject/warmup recovery rather than further arbitrary admission
tuning. A second enhancement queue requires per-pair motion/depth and
independent fence ownership; do not share the current monotonically
signaled fence across unordered queues or assume concurrency is faster.
Neither disabling NR, changing the 4K contract nor actual4X is fixed6X
acceptance. The active goal remains open; shutdown is not yet performed.

# Fixed-media DLSS cadence repair

User authorized repair, checkpointing validated improvements, testing p001.mp4 with NR / 4K target / DLSS 6X and comparison to 2X, then shutdown after reporting. Starting checkpoint: dad189a, tag checkpoint/pre-fg-cadence-repair-20260920. Work remains on codex/fg-cadence-audit-20260920; no publish authorization.

Media: E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4. ffprobe reports H.264 yuv420p 3840x2160 60/1, AAC, 333.764 seconds. A 4K target does not upscale an already 4K source; report actual graph sizes and enabled stages rather than claim SR execution from its UI flag.

1. Baseline matched file playback 2X/6X and bounded live replay as needed. Use original NR realtime policy and fixed 4K target, same display size, pacing disabled. Never interpret software FPS as scanout.
2. Inspect submission, temporal recovery and deadline models; fix evidenced scheduling defects. Preserve real resets, bounded queues, audio speed and export behavior. Investigate increasing command slots or scheduling changes only with measured evidence and resource lifetime checks.
3. Compare stable cadence, source continuity, valid generated frames, PTS, latency and CPU/GPU timing, not only average FPS. If 6X exceeds compute capacity, prioritize sustainable even output and retain requested quality/multiplier semantics honestly.
4. Build main program and affected tests; run CPU policy tests, GPU reset/admission regression and fixed-media playback. Each run <=300 seconds. Commit verified improvements individually. Record failures and reverted experiments.
5. Write final report with rollback points, available build path and unmeasured display/hardware boundaries, then request Windows shutdown as authorized.

New outputs: E:/项目/Veyra/tests/fg-cadence-repair-20260920, logs/fg-cadence-repair-20260920, tmp/fg-cadence-repair-20260920. Reuse build/slider-reset-20260919; no source-tree artifacts.

## Implementation and rejected experiments

- File presentation now resolves each frame's own fence and interpolation status, as capture already does, instead of waiting for the entire generated batch.
- Moving DLSS preview measures base GPU work and cost per generated frame over 60 samples. After 30 samples, persistent overload lowers the effective multiplier (15 confirmations); sustained headroom restores it (120 confirmations). Down/up headroom thresholds are 90%/82% of the source interval. The saved request remains unchanged and UI reports the scheduled multiplier separately. Unknown cost preserves the request. This is a measured-cost heuristic, not a guarantee of scanout cadence.
- The graph uses the effective number for NGX evaluations and interpolation PTS. Queued batches keep their own size for pacing. Export/default graph calls retain the configured full multiplier. Provider-owned XeSS/FSR pacing is excluded from this DLSS controller.
- Combining all MFG evaluations into a single command list reduced measured output to about 72 FPS; reverted. Servicing older presentations during producer slot waits reached only about 89 FPS; reverted. No CommandSlotRing changes retained.
- XeSS is included in the active goal. Test the same media/effects at 2X and 4X, checking SDK generation counters separately from application Present calls; provider-owned subframe timing needs separate evidence.
- Final acceptance: sustained matched DLSS 6X/2X; XeSS 2X/4X; dynamic multiplier GPU validity/PTS and recovery tests; CPU policy and application lifecycle/startup. Results belong in FG_CADENCE_REPAIR_ACCEPTANCE_2026-09-20.md.

## Fixed 6X follow-up: not yet accepted

User explicitly requires fixed 6X cadence repair in addition to the fallback checkpoint 0050870. Tests below bypass the capacity controller; multiplier remains 6. None proves NR-on fixed 6X repaired.

| Run | Settings / duration | Retained submission FPS | P95 interval ms | Complete six-frame batches |
| --- | --- | --- | --- | --- |
| fixed6-slots16 | NR on, 16 command slots, 45s | 61.196 | 18.0236 | 0 |
| fixed6-nr-off | NR/SR off, original 6 slots, 30s | 360.002 | 3.3488 | 371 |
| barrier6 | NR on, avoid repeated input/depth COMMON transitions, 45s | 77.230 | 17.7609 | 0 |
| phase20-slots16 | NR on, 16 slots, file presentation phase delayed 20ms, 45s | 62.609 | 17.7917 | 0 |

Each trace is only the final bounded retention window, not the entire test. NR-off retained 6.206s, max interval 4.1553ms, no interval over16.667ms, PTS steps 2.7778ms. This demonstrates valid full6X submission with headroom; disabling NR is diagnostic, not a proposed user solution. NR-on measured GPU work is about17-18ms per60Hz input versus16.667ms budget. Enlarging the ring removed CPU slot waits but did not repair deadline losses. This does not prove all observed stalls are unavoidable compute limits.

Input/depth barrier experiment and fixed20ms phase experiment were reverted for lack of benefit. The latter also added video phase lag and is not an audio synchronization fix. Default command ring remains6. Two bounded test hooks remain: VEYRA_TEST_COMMAND_SLOTS (6..24), VEYRA_TEST_FIXED_FG_MULTIPLIER (presence bypasses automatic multiplier selection). Normal runs do not set them.

Evidence: E:/项目/Veyra/tests/fg-cadence-repair-20260920/{fixed6-slots16,fixed6-nr-off,barrier6,phase20-slots16}, corresponding stdout/stderr/JSON files; build logs slots-build.log, barrier-build.log, phase-build.log in E:/项目/Veyra/logs/fg-cadence-repair-20260920. All bounded harness runs exited0 for lifecycle only: minimumTargetRatio=0 deliberately disables a target-throughput assertion. No scanout measurement or subjective smoothness claim follows from these numbers.

Next investigation must address the overload equilibrium: admitting work by the last generated deadline permits earlier subframes to expire, and repeated reject/warmup transitions remove additional temporal coverage. A fixed phase shift cannot cure a sustained throughput deficit. Any replacement must preserve original media time/audio speed, bounded queue/latency, accurate effective multiplier reporting and reset/resource lifetime correctness. XeSS final regression and lifecycle acceptance remain pending. Goal remains active; no shutdown until final work/report is complete.

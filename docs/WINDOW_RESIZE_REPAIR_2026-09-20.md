# Window resize crash repair (2026-09-20)

Scope: user reported a freeze/crash while holding and dragging a window corner
in the 1.4.3 test build. Preserve retained frame-generation repairs and timing.

## Evidence and repair

The old executable crashed during actual bottom-right mouse resizing with
capture active and all effects off. PID 22100 dump reports 0xc000041d at
__chkstk+0x37 with exhausted UI-thread stack. User PID 408 dump has the same
stack exhaustion pattern. Raw stack scans are supporting evidence, not a
complete formal unwind trace.

PE x64 unwind metadata establishes callback stack allocations:

| Callback | Old bytes | Fixed bytes |
| --- | ---: | ---: |
| AppShell proc | 162360 | 280 |
| SettingsWindow proc | 98216 | 2536 |

Win32 modal sizing, layout and painting reenter window callbacks. Previously
each invocation reserved large storage even for small messages. Extract heavy
creation, command, timer and related handlers into noinline helpers, retaining
message bodies, return/break semantics and dispatch order. Do not increase the
thread stack to conceal excessive callback allocations. Rendering, GPU fences,
NR, FG multipliers and latency policy are unchanged.

Changed product files: apps/veyra/ui/AppShell.cpp and
apps/veyra/SettingsWindow.cpp. Regression tools: ui-native-resize.py and
ui-stack-budget.py under scripts/acceptance.

## Verification

- Release 1.4.3 build: passed; UI contract and presentation worker tests passed.
- Stack budget check: fixed build passes; old executable fails as expected.
- Actual mouse bottom-right shrink and top-left drag/snap with NV12 capture:
  video continues, UI responds, clean Alt+F4 shutdown.
- Native Windows modal sizing: four grow/shrink cycles each for capture,
  NR + DLSS 6X and NR + XeSS 4X: passed with continuing presentation.
- Synthetic DPI 96/144/192/reset cycles: passed. Only one physical monitor was
  connected; this does not certify real cross-monitor transitions.
- Baseline synthetic resizing alone did not reproduce the actual mouse crash;
  actual mouse evidence and compiled stack budget are required regressions.
- Initial DLSS test result reader failed on non-UTF8 diagnostic bytes after
  successful app exit. Reader corrected to tolerate those bytes; rerun passed.

Build command: scripts/build-isolated.ps1 -Root . -BuildDirectory
E:/项目/Veyra/build/slider-reset-20260919 -DependencyCache
E:/项目/Veyra/build/slider-reset-20260919/CMakeCache.txt -TempDirectory
E:/项目/Veyra/tmp/fg-cadence-repair-20260920 -Targets
veyra,veyra_presentation_worker_tests,veyra_ui_contract_tests -DisplayVersion 1.4.3.
Linker /MAP enabled in the local cache for stack-budget inspection.

Evidence: E:/项目/Veyra/tests/resize-hang-20260920/ (fix-build.log,
fixed-mouse-capture.log, fixed-native-capture, fixed-native-dlss6-rerun,
fixed-native-xess4, fixed-dpi). OS crash dumps remain in the Windows-managed
C:/Users/123/AppData/Local/CrashDumps directory; no global WER settings changed.

Local package command: scripts/package-portable.ps1 -Root . -Version 1.4.3
-Label '-test' -BuildDirectory E:/项目/Veyra/build/slider-reset-20260919
-OutputDirectory E:/项目/Veyra/test-packages/1.4.3-20260920-resize-fix.
Process TEMP/TMP: E:/项目/Veyra/tmp/fg-cadence-repair-20260920.

Coverage is RTX5070 on this machine, not all users/devices. Fixed 6X pacing
gaps remain unresolved. No push or public release.

## Package verification

Portable smoke: all seven cases passed using visible-scene.mp4, CaseSeconds 7,
including real NR/SR/FG and app-local module checks. Logs/result:
E:/项目/Veyra/tests/resize-hang-20260920/package-smoke/.
ZIP contents: all 120 payload entries verified against package manifest sizes
and SHA256, plus the manifest itself; publisher audit forbiddenFiles=0.
Unchanged runtime identities and patched FFmpeg retained.

Packaged executable native modal resize with NR + SR + DLSS 6X passed all four
grow/shrink cycles (1280x800 to 1544x1064 and back), exit 0; evidence in
package-native-resize/result.json. Packaged --smoke-ui --smoke-seconds 12
passed F11/Esc, Alt+Enter/double-click and remaining UI checks, exit 0
(package-ui.log).

Additional --smoke-repair-ui check failed at
NR_off_click_works_with_invalid_draft on both the fixed build and old delivered
package with identical NR-on/SR-off/FG-off input. Logs fixed-repair-ui.log and
baseline-repair-ui.log preserve this existing failure. It is not counted as a
pass and is outside the resize-only repair; the invalid draft condition needs
separate investigation. Actual NR-disable request and graph application are
present in the fixed log.

ZIP: E:/项目/Veyra/test-packages/1.4.3-20260920-resize-fix/Veyra-1.4.3-test-win64-portable.zip

Size: 472349977 bytes.
SHA256: ABA2DA87D1A308629BE1CD347401DF266212D98BC9A5E904E078512AE6FADD19.

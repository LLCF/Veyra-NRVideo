# Veyra

<p align="center"><img src="assets/veyra-logo.png" alt="Veyra" width="200"></p>

English | [简体中文](README.md)

<p align="center">
  <a href="https://github.com/Likely7/Veyra-NRVideo/blob/main/REAMDE%20MP4.mp4">
    <img src="assets/readme-demo.gif" alt="Veyra demo video" width="960">
  </a>
</p>

<p align="center">The demo plays automatically; click it to open the original MP4.</p>

A Windows video player and capture-card enhancement tool. Play videos, process images, and preview capture devices with optional super resolution, NR enhancement, and frame generation.

[Download 1.4.1 Portable](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.1) · [Release Notes](docs/RELEASE_NOTES_1.4.1.md) · [Report an Issue](https://github.com/Likely7/Veyra-NRVideo/issues)

## 1.4.1 Update

**RTX 30/40 DLSS frame generation fixes, up to 6X**: fixes initialization failures, unintended 2X fallback and RTX 3060 freezes when enabling FG. Also repairs sustained-playback scheduling that could leave FG limited despite low total GPU utilization, plus 6X batch capacity, timestamps and presentation synchronization. RTX 50 retains its native path. Affected users report successful testing; compatibility unlocks remain community experimental, with no claim that every GPU/driver combination was tested.

**Playback and capture**: selectable embedded primary/secondary subtitles and audio tracks, faster large-MKV opening and seeking, persistent NR/SR settings, sleep/screensaver prevention during playback, consistent presentation when enabling NR before XeSS, and recovery from audio-output faults without unnecessarily restarting video capture.

**Export**: NVENC ABI compatibility and clearer errors; removes qualification gates and the final full-file frame-by-frame decode, preserves variable frame timing and selected audio, and automatically uses HEVC Main10 for HDR requests with H.264 selected. See the [complete 1.4.1 notes](docs/RELEASE_NOTES_1.4.1.md) for changes, evidence and limits.

## 1.4.0 Update

**New colour page**: a full-float grading chain that runs *before* every effect stage (zero cost while the master switch is off). The panel follows Lightroom's layout and feel: gradient rails, a point-curve editor with draggable control points (monotone cubic spline, both black/white points free to move), four colour-grading wheels, an 8-colour mixer strip with a black & white switch, `.cube` LUTs with an input-space check, named colour presets with `.vpcolor` import/export, undo/redo, hold-to-see-original, and a per-section "eye" that temporarily disables a group. Everything is graded in linear light with output dithering to avoid banding; HDR is graded in the linear domain, before tone mapping.

**Fixes**: HEVC-in-MKV files that would not open (new D3D11VA hardware path), failed exports caused by whole-slot frame gaps (an honest repeat of the previous frame), colour parameters that were not live (or did not refresh while paused), a `.cube` load crash, exports that ignored the grade, PS5 streaming connect failures, capture formats that stalled after two frames, capture windows that streaming tools could not find, silent Dolby/DTS capture, AVerMedia 5.1 passthrough and 40+ more - itemised in the [release notes](docs/RELEASE_NOTES_1.4.0.md).

**Removed**: the AMD FSR *frame-generation* entry in the panel (switching back to DLSS was easy to get stuck on and the result was mediocre); the engine backend stays.

<p align="center"><img src="docs/images/1.4.0/mjpeg-latency.png" alt="MJPEG capture chain before/after" width="900"></p>
<p align="center"><small>MJPEG capture chain, two-minute real-hardware runs: 1080p60 processCpu 1.987 to 0.364-0.375 ms (-81%), 4K18 6.218 to 0.912-0.936 ms (-85%)</small></p>

<p align="center"><img src="docs/images/1.4.0/native-latency.png" alt="Native (YUY2/NV12/RGB) capture chain before/after" width="900"></p>
<p align="center"><small>Native capture chain: 1080p60 P95 unchanged, 4K18 P95 -7.5% and processCpu -8.7%, zero drops on both sides.</small></p>

<p align="center"><img src="docs/images/1.4.0/colour-latency.png" alt="Colour chain on/off latency" width="900"></p>
<p align="center"><small>Colour chain on/off (YUY2 1080p60 capture, two minutes each): +0.037 ms GPU per frame, no measurable software latency change, 60 fps and zero drops in both.</small></p>

## 1.3.0 Update

Version 1.3.0 integrates playback, capture-audio, color and frame-generation repairs since 1.2.0. It adds drag previews, fullscreen 10-second seeking, remembered capture settings and NR internal resolutions; improves AV1/MOV playback, capture sync/crackle, SDR colors and HDR-to-SDR mapping; and submits live DLSS MFG subframes as each becomes ready. The main FPS display reports submitted frames.

See [1.3.0 release notes](docs/RELEASE_NOTES_1.3.0.md) for the full list and limits. Full Dolby Vision, Atmos object rendering and precise internal XeSS FG GPU timing remain unavailable. Hardware-specific validation still applies.

## Features

| Feature | Options |
| --- | --- |
| Playback and capture | H.264 / HEVC / AV1 and compatible MOV/ProRes video, PNG / JPEG images, DirectShow / UVC capture cards |
| Super resolution | DLSS SR and RTX Video SR; 1440p / 4K / 8K targets with aspect ratio preserved |
| NR enhancement | Experimental NVIDIA NR; realtime and native modes, style, intensity, and region protection |
| Frame generation | DLSS 2X / 3X / 4X; experimental XeSS 2X preview |
| Smooth Motion | NVIDIA App driver-based frame generation, enabled manually; in-app setup guide |
| Controls | Live settings, restore defaults, original/processed comparison, split view, preview zoom |
| Export | PNG / JPEG images; NVENC H.264 / HEVC video |
| Diagnostics | Completed source/generated frames, presentation submissions, stage timings, software latency |

Daily mode focuses on watching. Professional mode expands the controls, diagnostics, and export tools without reopening the video. The application currently uses a Chinese interface.

Features introduced in 1.2.0 include HDR file/capture enhancement, HDR10 export and HDR screenshots, retained 5.1 PCM, and a live Convert to SDR display switch. Fresh installations still disable all enhancements. Affected users have confirmed the 1.4.1 RTX30/40 repairs; other hardware combinations require individual testing.

## Download and Run

1. Download **Veyra-1.4.1-win64-portable.zip** from [Releases](https://github.com/Likely7/Veyra-NRVideo/releases). The Source code archives are for developers.
2. Extract the entire archive into a writable directory and run **Veyra.exe**. No SDK, Python, or development tools are needed.
   NR, upscaling and internal frame generation start disabled. Enable them as needed; importing old preferences restores their switches.
3. Use a current GPU driver. NVIDIA NR, DLSS, RTX Video SR, and NVENC require compatible NVIDIA RTX hardware; this version was primarily tested on an RTX 5070.

Requirements: Windows 11 x64 and DirectX 12. Required application runtimes are included; GPU and capture-device drivers are supplied by the system. 8K upscaling and native-resolution enhancement need more VRAM and may not run in real time.

To upgrade, close the old version and extract into a new directory. To retain settings, copy the old `runtime_local/*.v1` files and `veyra.ini`. Do not overwrite the new runtime directories with the old ones.

## PS5 Streaming

1. Connect the PS5 and PC to the same LAN, preferably by Ethernet. Enable **Settings → System → Remote Play** on the console.
2. Open **PS5** in Veyra and search. If discovery fails, enter the IPv4 address from **Settings → Network → Connection Status → View Connection Status**.
3. Enter the **PSN Account ID**, not a nickname or Online ID. Experimental PSN sign-in opens Sony's website. Complete sign-in, copy the final redirect URL, then submit it through Veyra's clipboard button.
4. Initial pairing still requires the eight-digit code from **Remote Play → Link Device**. Afterwards select the saved console and connect without pairing again.
5. Connect the gamepad to the **PC**; USB is preferred for DualSense. Touchpad, gyro, vibration and adaptive triggers depend on device, connection and game. View-only disables PC input forwarding; it does not guarantee same-account coexistence with a PS5-connected controller.

Stream input is up to 1080p. 2K / 4K / 8K are local upscaling targets. H.264 / H.265 and 5–100Mbps bitrate requests are available; console output may differ. Auto decoding tries hardware then software; manual choices are available.

Pairing and PSN credentials are encrypted in **%LOCALAPPDATA%/Veyra/remoteplay**, bound to the Windows user and retained across upgrades. Do not share this directory. Signing out retains console pairing. Pinless first registration and Internet streaming are not implemented.

**Experimental HDR:** Windows HDR output can retain HDR with NR, SR and FG enabled. NR / RTX Video SR process an SDR proxy and composite changes onto the retained HDR base; this is not a native HDR NR model. SDR displays still use tone mapping. Real PS5 HDR and Sony authorization need further validation.

## Quick Guide

### Videos and Images

Choose Open on the bottom bar. Playback, seeking, volume, subtitles, and fullscreen are available there. In Professional mode, use the mouse wheel over the picture to zoom.

### Capture Cards

1. Connect the device and close other applications using the same capture card.
2. Choose Capture, then select the device, resolution, frame rate, pixel format, and audio input.
3. In Audio monitoring, choose a `[DirectShow]` device or an explicit `[WASAPI]` endpoint; the default is no audio monitoring. WASAPI stores the Windows endpoint ID and connects only to the selected input; it does not fall back to a microphone or system loopback.
4. Confirm the picture with enhancement disabled, then enable NR, upscaling, or frame generation. Try YUY2 / NV12 when the device offers the same desired mode.
5. For a 30fps console game carried over 60fps capture, select the 60-to-30 content cadence setting in Professional mode. Keep the original cadence for actual 60fps content.

### Enhancement and Frame Generation

Enable NR, super resolution, and frame generation independently in Professional mode. Select the upscaling method and target size beneath the super-resolution switch. The frame-generation page offers DLSS / XeSS and the supported multipliers.

Start with realtime NR, a lower RTX Video SR quality, and 2X frame generation. Compare the image and watch the diagnostics. Reduce quality, multiplier, or target size if processing falls behind. The main chart reports enhancement GPU processing time; estimated extra picture delay is separate in the detailed view. Frame generation does not reduce game input latency.

Audio synchronization follows the software processing chain without counting the capture card's shared input delay twice. Small timing fluctuations no longer stop audio on each frame. Sustained GPU overload skips expired preview opportunities while audio and media time continue; reduce workload to retain more video frames.

Hover over settings for help. Default order is Upscale → NR → Frame generation. Optional Low latency mode uses NR → Upscale → Frame generation for preview only; it may reduce cost but increase ghosting or edge artifacts.

### Smooth Motion (NVIDIA App)

To use Smooth Motion alone, set Veyra's frame-generation multiplier to **Off**, then enable **Smooth Motion** for the current **Veyra.exe** in **NVIDIA App → Graphics** and restart the player. NR and super resolution can stay enabled. The professional frame-generation panel includes an expandable guide.

For internal DLSS / XeSS only, disable Smooth Motion in NVIDIA App and restart before selecting internal generation. Stacking both is also allowed without blocking; its quality and performance have not been validated, and ghosting, latency or GPU load may increase. Veyra's generation and master-enhancement switches do not disable driver generation.

Software FPS, timings and queues exclude driver-generated work. Screenshots and exports do not include driver-generated intermediate frames. A/V timing and recording capture require separate verification. A user reported effective, stable operation on this machine; this is not validation of every GPU or driver.

### Export and Runtime Replacement

Use Professional mode to save an image or export a video, then choose the format and destination. XeSS is preview-only; supported video frame-generation export uses DLSS.

You may replace DLLs while Veyra is closed. NVIDIA components belong in `runtime/experimental/`; XeSS / XeLL belong in `runtime_local/intel/experimental/`. Keep the filenames. Veyra does not enforce hash or signature locks; manifests describe the shipped files only. Replacement versions may have incompatible APIs or hardware requirements. To uninstall, close Veyra and delete its extracted directory.

### NR Runtime Selection

In Professional mode, select the NVIDIA original, community RTX40/50, or **RTX30 compatibility · Experimental** runtime, then enable NR. Switching briefly interrupts playback; failed changes restore the previous configuration. Community variants are included separately in `runtime/experimental/nr-community/` and `nr-ampere/`; both have Authenticode status `HashMismatch`.

All three runtime paths were tested on RTX5070. Affected users report the 1.4.1 RTX30/40 repairs working; performance and image quality depend on the GPU, content and settings. NR runtime selection is separate from DLSS frame generation. A dedicated FG compatibility layer provides up to 6X on RTX30/40.

### OBS Streaming and Recording

Add a **Window Capture** source, select Veyra, and explicitly set **Capture Method** to **Windows 10 (1903 and up)**. Automatic may choose BitBlt, capturing the controls but missing the GPU-rendered video. Switching to the Windows capture method restored video in the reported local test.

Veyra's experimental broadcast compatibility switch only changes the presentation swapchain; it does not fix BitBlt capture. Leave it off unless testing a specific capture issue. In other recording applications, prefer Windows Graphics Capture / WGC. Compatibility with every recorder and the capture cadence of generated frames have not been verified.

## Technical Approach and Limits

```text
Video / image / capture card / PS5 -> color handling -> SR -> NR -> frame generation -> display / export
```

C++20, Win32, and D3D12. FFmpeg handles media files, DirectShow handles capture, and NVENC handles video encoding. Inputs share one enhancement graph; capture retains the latest frame, and optical flow supplies estimated motion.

NR and DLSS frame generation are **community-experimental integrations**, not NVIDIA certification or complete native game integration. Captured pixels lack game-engine depth and motion data; ghosting and altered detail are possible. AMD NR is unavailable, FRUC has been removed, and AV1 / ProRes export are unsupported; development HDR support is scoped below. Capture compatibility and long-term stability remain under testing.

## Development and License

[Build Instructions](docs/BUILD.md) · [Runtime Components](docs/RUNTIME_COMPONENTS_1.4.1.md) · [Third-Party Notices](THIRD_PARTY_NOTICES.md)

Original Veyra source is [GPLv3](LICENSE); the combined streaming program also falls under [AGPLv3 and the upstream OpenSSL exception](licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt). Application source matches the release tag. The RemotePlay-source and FFmpeg-source assets provide dependency source and are not needed to run the player. SDKs, models, and runtimes are excluded from this source repository. Release components retain their separate licenses and experimental distribution boundaries.

Stage cards and the main chart report enhancement GPU processing times. Extra picture delay is estimated separately in the detailed view; neither is measured button-to-screen latency. Files can be processed ahead. Sustained overload skips expired preview frame opportunities to keep media time advancing and audio continuous; export retains complete processing.

### Experimental GPU DIS motion

Select GPU DIS · FAST in the optical-flow menu to compare results. NVOF remains
the default. This implementation was substantially slower than NVOF on our RTX
5070 in a 1080p synthetic test; a performance improvement is not promised.

Video export validates completeness before success and may take extra time to
finish. Embedded subtitles are not preserved. Avoid simultaneous exports from
multiple instances to the same target file.

In Professional mode, click **Screenshot** in the top toolbar to save the latest processed full-resolution picture under **Pictures / Veyra Screenshots**: PNG for SDR and floating-point JPEG XR (`.jxr`) for HDR output. Use an HDR-capable viewer. Application UI and window zoom are excluded.

## HDR and 5.1 (introduced in 1.2.0, retained in 1.3.0)

These capabilities were introduced in 1.2.0 and are retained in 1.3.0. Individual HDR displays, 5.1 endpoints, and capture cards still require hardware acceptance.

- Files, P010/P016 capture and PS5 can use explicitly described BT.2020 NCL PQ/HLG input. Windows HDR enables retained HDR output with NR, DLSS SR / RTX Video SR and DLSS / XeSS FG. NR / Video SR use an SDR proxy plus the retained HDR base, with reduced changes near black and compressed highlights. This is not native HDR NR inference. HLG uses a 1000-nit, gamma-1.2 reference conversion.
- Capture defaults to device color metadata. Manual PQ / HLG is available for devices that omit it, requiring P010/P016. Ten-bit storage alone does not identify HDR. RGB/YUY2 HDR and BT.2020 constant-luminance input are unsupported.
- HDR video export uses HEVC Main10 / BT.2020 / PQ, including HLG-to-PQ conversion and optional NR, SR and internal DLSS FG. XeSS remains preview-only. H.264 HDR export is rejected. HDR screenshots use lossless scRGB FP16 JPEG XR. Original mastering/peak metadata is not invented or reused after processing.
- File and capture PCM preserve speaker positions through one audio clock, compensation and volume path. Capture tries actual multichannel device formats first. Configure the Windows endpoint for 5.1; stereo endpoints receive an explicit downmix. Detailed status reports input/output channel counts. PS5 remains stereo. Compressed Dolby/DTS passthrough and Atmos object audio are not implemented.

RTX 5070 GPU output, HDR export and software channel isolation have local test evidence. HDR display appearance, real 5.1 speaker positioning and individual capture cards require hardware acceptance. See the [execution record](docs/HDR_MULTICHANNEL_EXECUTION_2026-09-14.md). Fresh-install effects remain off.

The capture panel also offers **Convert to SDR display**, off by default. It controls all live previews, maps HDR to SDR without changing input metadata or disabling enhancement, and can be toggled during playback without reconnecting. Disabling it follows the display HDR state. Screenshots follow the preview; video export retains its existing HDR policy.

## Acknowledgments

Thanks to [Magpie Experimental](https://github.com/SAOG0721/Magpie/tree/experimental) for research insights into NR residual composition, optical flow, and enhancement pipelines; to [chiaki-ng](https://github.com/streetpea/chiaki-ng) for the PS5 streaming foundation; and to [XeSS-GPU-Motion](https://github.com/gggz114514-oss/XeSS-GPU-Motion) for its GPU DIS optical-flow implementation.

See [Third-Party Notices](THIRD_PARTY_NOTICES.md) for other dependencies, sources, and licenses.

## Support and feedback

If this project helped you, you can buy the author a coffee (WeChat QR below). For bugs, or to get beta builds first, join the group.

<p align="center">
  <img src="docs/images/1.4.0/donate-wechat.jpg" alt="WeChat donation" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="docs/images/1.4.0/community-group.jpg" alt="Veyra community group / bug reports / beta builds" width="220">
</p>
<p align="center"><small>Left: WeChat donation (voluntary; no feature is ever gated behind it) - Right: Veyra community group for bug reports and beta builds.</small></p>

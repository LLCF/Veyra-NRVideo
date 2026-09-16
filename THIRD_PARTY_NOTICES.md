# Third-party dependencies

NVIDIA SDKs and runtimes are excluded from source control. The publisher-authorized experimental Release package contains only selected runtime DLLs and applicable notices, as documented in `docs/RUNTIME_COMPONENTS_0.0.2.md`. This is not vendor endorsement or a general redistribution grant. ReShade/RenoDX add-ons are not loaded or distributed by the product.

## NVENC API declarations

Source: https://github.com/FFmpeg/nv-codec-headers ; checkout `eddcea9e27f6b772057c9b3f87de2cc1737faffc` (SDK 13.1.15 declarations), stored only under ignored `third_party_local/nvidia/nv-codec-headers`.

The nvEncodeAPI.h header itself has NVIDIA's permissive MIT-style notice (Copyright 2010–2026 NVIDIA Corporation). Its full notice is retained unmodified in that header. This permits using the declarations without retrieving the full developer-portal sample package; it does not grant rights to distribute NVIDIA driver/runtime binaries. Veyra uses the system NVENC library, never copies it into a package. Implementation is independently authored against these declarations; no competitor/sample implementation copied.

FFmpeg: dynamically linked 9.0.1#1 vcpkg build. The portable package carries five FFmpeg DLLs, the complete copyright/license notices and SPDX provenance. Corresponding upstream source and the vcpkg patch/build recipe are listed in `docs/BUILD.md`. No FFmpeg command-line executable or test-media toolchain is shipped.

The PS5 H.264 repair build additionally applies `scripts/ffmpeg/ps5-h264-slices.patch` to FFmpeg's LGPL `libavcodec/h264dec.h`, increasing the bounded slice capacity from 32 to 256. This is a Veyra modification, not an upstream release claim. Its corresponding source, patch, configuration and DLL identity record must accompany any future binary release; the previously published 0.0.5 package is unchanged by this local repair.

## Intel XeSS / XeLL

Official XeSS SDK 3.0.2. Veyra loads `libxess_fg.dll` and `libxell.dll` for experimental preview frame generation. Unmodified binaries may be redistributed under the Intel Simplified Software License; the complete license and `third-party-programs.txt` accompany the package. User DLL replacement is allowed by Veyra without fixed identity locks; compatibility is not guaranteed.

### XeSS multi-frame unlock (ported, process-memory only)

Source: https://github.com/Coldwood1026/OptiScaler , commit `70676c5f037c8c26f1ec355b250a72303cd268da`, files `OptiScaler/proxies/XeFGUnlock.h` and `XeFGPacing.h` (GPL-3.0). The five byte patches that raise the provider's generated-frame ceiling on non-Intel GPUs are ported into `include/veyra/gfx/XessMfgUnlock.h` / `src/gfx/XessMfgUnlock.cpp`; the structural design (module identity checks, transactional install, rollback after the contexts exit) follows `SAOG0721/Magpie` (`experimental` branch, GPL-3.0, `XeSSFGCompatibility.h` / `XeSSFGPatchTransaction.h`). Veyra adds size + SHA-256 + PE identity checks and refuses unaudited provider builds.

The provider DLL on disk is never modified, re-signed or renamed; only the mapped image of the process is patched, and every patched byte is restored when the XeFG/XeLL contexts are destroyed. Because Veyra itself is GPLv3, the ported GPL-3.0 code is compatible; the upstream authorship above is attributed here. Locked provider identity: `libxess_fg.dll` 1.3.1.78, 22,957,432 bytes, SHA-256 `EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27`, PE TimeDateStamp `0x69CB0F4D`, SizeOfImage `0x015ED000`.

## AMD FidelityFX Optical Flow

FidelityFX SDK 1.1.4, upstream commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK . The optical-flow and DX12 backend libraries are statically linked. Copyright (C) 2024 Advanced Micro Devices, Inc.; MIT license, reproduced in the package's `licenses/AMD_FIDELITYFX_LICENSE.txt`. This is optical flow, not AMD NR or AMD super resolution.

## Microsoft Visual C++ Runtime

The package includes unmodified x64 `vcruntime140.dll`, `vcruntime140_1.dll`, and `msvcp140.dll` from the Visual Studio 2022 C++ Redistributables directory for FFmpeg and XeSS. These are Microsoft Distributable Code under the [Visual Studio software terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/); Windows system and GPU driver DLLs are not copied. Veyra itself uses the static MSVC runtime.

## Lucide UI icons

Source: https://github.com/lucide-icons/lucide/tree/a537cb6eb323b885f4c60baf3cec1a995982d167

24 native icon mappings use 23 Lucide SVGs, retained under `assets/icons/lucide/`. Lucide is ISC-licensed (Copyright 2026 Lucide Icons and Contributors); its Feather-derived subset is MIT-licensed (Copyright 2013-present Cole Bemis). The complete upstream notices are preserved in `assets/icons/lucide/LICENSE` and must accompany any distribution containing these icons.

`assets/icons/lucide/manifest.json` records the pinned revision and per-file SHA256. `scripts/generate-lucide-icons.py` converts the SVG geometry to GDI+ paths in `apps/veyra/ui/LucideIcons.h`, using development-only fonttools 4.64.0. Veyra requires no fonttools, network request, icon font or external icon runtime.

## RTX Video SDK 1.1 local VSR adapter

User-provided official SDK from https://developer.nvidia.com/rtx-video-sdk/getting-started, kept under ignored third_party_local/nvidia/RTX_Video_SDK_1.1.0. Its NVIDIA RTX SDK license remains applicable. VideoSrBackend is independently authored against the documented VSR parameter ABI; no SDK sample implementation or proprietary header is copied into the repository. Local nvngx_vsr.dll remains excluded from Git. Under the user-authorized 2026-09-09 Release Runtime Pack policy, its pinned signed release copy may be included only as a Release asset alongside the applicable SDK license and manifest; it is never committed to source control.

## NVIDIA FRUC

FRUC was evaluated during development and has been removed from the product. No FRUC runtime, worker, SDK headers, or binaries are built or packaged.

## PS5 Remote Play / chiaki-ng (0.0.5)

The optional `VEYRA_ENABLE_REMOTEPLAY` build compiles chiaki-ng at
`0e16950165f06e5c3291537c2eeba6e852be7120` from https://github.com/streetpea/chiaki-ng,
with the two reviewed patches in `scripts/remoteplay/patches/`. Chiaki code and
these derived patches retain AGPL-3.0-only with the upstream OpenSSL exception;
see `licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt`. The metadata patch was adapted
from the user-supplied Code 01 change description and checked against this pin.
No Chiaki binary or third-party SDK is committed. The 0.0.5 distribution provides its corresponding complete source,
these patches, dependency pins, build instructions and upstream notices; the
existing Veyra GPL file alone is not the combined program's license record.

Gamepad input uses SDL 3.4.14 (https://github.com/libsdl-org/SDL/tree/release-3.4.14),
statically built using vcpkg. SDL provides DualSense and other controller device
support; Veyra maps its public gamepad API to Chiaki semantic input. SDL also routes DualSense haptic audio; media playback remains in the shared Veyra engine. Retained notices: `licenses/remoteplay/SDL3_NOTICES.txt`.
The 0.0.5 Release contains the combined executable. Its application source is the release tag; the RemotePlay-source asset supplies pinned upstream sources and build recipes.
# Remote Play controller additions (2026-09-12)

Veyra calls the pinned Chiaki orientation tracker API (AGPL-3.0-only with OpenSSL exception). DualSense SDL report offsets and 3 kHz stereo to four-channel haptic routing were checked against chiaki-ng gui/src/controllermanager.cpp and streamsession.cpp. Existing Chiaki and SDL license notices apply. No third-party binary was added.

### PSN browser authorization adapter (2026-09-12)

`src/remoteplay/PsnAuth.cpp` adapts the OAuth request contract and public client identifiers from chiaki-ng, commit `0e16950165f06e5c3291537c2eeba6e852be7120`, `gui/src/psnaccountid.cpp`, `gui/include/psnaccountid.h`, and `gui/src/psntoken.cpp`. Upstream credits the Account ID script to grill2010. License: AGPL-3.0-only with the upstream OpenSSL exception. The Windows HTTP, protected storage and UI integration are Veyra code. This is an unofficial client, not Sony endorsement. json-c remains an existing Chiaki dependency; its license/notice must remain in binary distributions.

Static dependency notices and installed SPDX records: licenses/remoteplay/{json-c,libevent,miniupnpc,openssl,opus,sdl3}/. Upstream curl, nanopb, Jerasure and gf-complete notices are in the same directory. Their corresponding sources and port recipes accompany the RemotePlay-source release asset.

## GPU DIS optical flow (experimental)

Source: https://github.com/gggz114514-oss/XeSS-GPU-Motion , commit
`cb7523b5104fc914dc501767c3139b43c2067af7` (public R4.2 snapshot).
The DIS provider and its shader closure are included under `third_party/gpu-dis/`.
Owned additions use Apache-2.0; OpenCV-derived portions retain their BSD/Apache
terms and Intel/Willow Garage/other upstream attribution. See that directory's
LICENSE, NOTICE, PROVENANCE.md, VEYRA_INTEGRATION.md, and licenses/.
Veyra adds GPU input/consumer adapters and descriptor staging; no Intel SDK,
worker dependencies or proprietary runtime is included in this source subset.
This is a motion-estimation option, not Intel XeSS frame generation itself.

## Acknowledgments and Project Sources

Thank you to these projects and their contributors. Integrated code, dependencies, and development references are identified separately below. Pinned versions, modifications, and licenses are recorded in the component sections above.

### Integrated Code and Dependencies

| Project | Contribution to Veyra |
| --- | --- |
| [chiaki-ng](https://github.com/streetpea/chiaki-ng) | PS5 Remote Play protocol, pairing and sessions; controller, haptics and PSN authorization integration and adaptations. Also credit to grill2010 for the upstream Account ID approach. |
| [XeSS-GPU-Motion](https://github.com/gggz114514-oss/XeSS-GPU-Motion) / [OpenCV](https://github.com/opencv/opencv) | Ported GPU DIS provider and shaders, retaining attribution and licenses for the OpenCV DIS-derived portions. |
| [FFmpeg](https://github.com/FFmpeg/FFmpeg) / [nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) | Media demuxing, decoding, muxing and NVENC API declarations. Releases provide the actual FFmpeg patches and corresponding source. |
| [SDL](https://github.com/libsdl-org/SDL) | PC controller input, DualSense support and haptic audio output. |
| [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | Optical flow and its D3D12 backend. |
| [Lucide](https://github.com/lucide-icons/lucide) | UI icons, retaining Lucide and Feather-derived license notices. |
| [Intel XeSS / XeLL](https://github.com/intel/xess) | Experimental XeSS frame generation and related runtime interfaces. |
| [NVIDIA DLSS](https://github.com/NVIDIA/DLSS), [RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk), [Optical Flow SDK](https://developer.nvidia.com/opticalflow-sdk), [Video Codec SDK](https://developer.nvidia.com/video-codec-sdk) | Upscaling, optical flow, encoding and enhancement interfaces. Experimental NR components have separate identities and boundaries in [Runtime Components](docs/RUNTIME_COMPONENTS_1.1.0.md); this is not vendor certification. |
| [vcpkg](https://github.com/microsoft/vcpkg) | Dependency builds, version records and license provenance. |

Remote Play also depends on **OpenSSL, Opus, json-c, libevent, miniupnpc, curl, nanopb, Jerasure and gf-complete**. Their attribution and license notices are retained in [licenses/remoteplay](licenses/remoteplay), with corresponding source supplied in Releases.

### Architecture, Implementation Ideas and Comparisons

| Project | Reference areas |
| --- | --- |
| [Magpie Experimental](https://github.com/SAOG0721/Magpie/tree/experimental) | NR / SR / FG combinations, configurable processing order, motion guidance, residual composition, resource synchronization and presentation pacing; an important practical comparison during development. |
| [OBS Studio](https://github.com/obsproject/obs-studio) | DirectShow capture, pixel formats, color metadata, buffering and window-capture behavior. |
| [DLSS5-NeuralScreen](https://github.com/perseval-BLR/DLSS5-NeuralScreen/tree/8098ccf261bedc16e4b5fe7887c51a07eb41720e) | RTX30 NR compatibility research and architecture-query behavior reference. Veyra's scoped adapter is independently implemented; the user-provided modified runtime has a separate identity and is not covered by the application's MIT license. See [local integration record](docs/RTX30_NR_AND_SAFE_DEFAULTS_2026-09-14.md). |
| [NVEnc](https://github.com/rigaya/NVEnc) / [RTXVideoProcessor](https://github.com/DrC0ns0le/RTXVideoProcessor) | Video upscaling, GPU frame resources, codecs and scheduling. FRUC was also researched; it has since been removed from Veyra. |
| [mpv](https://github.com/mpv-player/mpv) | RTX upscaling integration and video-processing approaches in a media player. |
| [video2dlssnr](https://github.com/DaniilSokolyuk/video2dlssnr) / [dlss5-nr-player](https://github.com/Zonnery/dlss5-nr-player) | Research comparisons for NR pipelines, stage order and data transfers; their code was not copied. |
| [dlss5-video-player](https://github.com/2600th/dlss5-video-player) / [dlss5-visual-enhancer](https://github.com/Merserk/dlss5-visual-enhancer) / [dlss5-infinity-studio](https://github.com/SamG-Coder/dlss5-infinity-studio) | Playback, offline enhancement, export and caching workflow references. |
| [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder) / [DLSS5-Reshade-AIO](https://github.com/kibblerz/DLSS5-Reshade-AIO) / [Assassin’s Creed Odyssey DLAA](https://github.com/SAOG0721/Assassins-Creed-Odyssey-DLAA) | Research into motion/depth inputs, color transfer, NR protection regions and temporal contracts. Veyra does not load or distribute ReShade / RenoDX add-ons. |
| [Video2X](https://github.com/k4yt3x/video2x) / [RIFE](https://github.com/hzwer/ECCV2022-RIFE) | Research into video upscaling and interpolation approaches; these algorithms are not integrated. |

<details>
<summary>Deferred AMD NR research references</summary>

[DLSS-NR-on-AMD](https://github.com/danielblnc/DLSS-NR-on-AMD), [dlss5-on-amd-9070xt-porting](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting), [dlss5-image-enhancer-zluda](https://github.com/RedDukeDev/dlss5-image-enhancer-zluda) and its [ZLUDA fork](https://github.com/RedDukeDev/ZLUDA), [dlss5-neural-amd](https://github.com/zmodelerlover/dlss5-neural-amd), and [DLSS5-AMD-Video](https://github.com/eikkapine/DLSS5-AMD-Video). These informed feasibility and performance research. AMD NR is not available in the current release.

</details>

## AMD FidelityFX SDK 2.3.0 (experimental FSR frame generation and FSR upscaling)

The player's AMD FSR backends (frame generation in the present sink and
upscaling in the enhancement graph) are written against the public FidelityFX
API documented and shipped in the AMD FidelityFX SDK 2.3.0 (MIT licensed). The
SDK itself is **not** vendored into this source repository: it stays in the
gitignored `third_party_local/amd/FidelityFX-SDK-2.3.0` tree, and the signed
provider/loader DLLs are used from the local
`runtime_local/amd/fidelityfx/` folder like the other experimental runtimes.
Integration code (`src/gfx/FsrFgPresenter.cpp`, `src/gfx/FsrSrBackend.cpp`) is an
independent implementation of the documented API sequence and was written
against these references:

- `Kits/FidelityFX/docs/techniques/frame-interpolation-swap-chain.md`
- `Kits/FidelityFX/docs/techniques/frame-interpolation-api.md`
- `Kits/FidelityFX/upscalers/include/ffx_upscale.h` and the FSR3 upscaler
  shader contract (`ffx_fsr3upscaler_reproject.h` for the motion convention)
- `Samples/Upscalers/FidelityFX_FSR/dx12/fsrapirendermodule.cpp`

Relevant negative finding, recorded so it is not re-litigated: a FidelityFX
frame-generation swapchain context keeps the real DXGI swapchain alive after
`ffxDestroyContext` (measured locally), so the player retains the proxy
swapchain for the window's lifetime. The FidelityFX upscale dispatch also does
not complete under D3D12 GPU-based validation (the standalone probe stalls
before its dispatch), so that validation mode is skipped for the FSR SR
configuration with an explicit log line. See
[AMD FSR frame generation integration record](docs/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md).

## RTX 40 series DLSS multi-frame unlock (ported)

Two upstream projects are ported into `include/veyra/ngx/AdaMfgUnlock.h` /
`src/ngx/AdaMfgUnlock.cpp`; both are MIT-licensed and are attributed separately.

### ImDreamt/MFGAdaUnlock-RenoDx (initial port)

https://github.com/ImDreamt/MFGAdaUnlock-RenoDx , commit
`a8aa0d9289471facc1e32013d17fd9c1195bc2cd` (MIT). The initial port brought:

- the two architecture compares (`0x1b0` → `0x190`);
- the kernel PTX midpoint correction (inject the temporal parameter, replace the
  104 compiled-in `0.5` multiplies, truncate the fatbin so the driver JITs the
  corrected code instead of loading the precompiled cubin);
- the `dlfg_kernel` descriptor redirect, all process-memory only.

### dashdogy/RTX40MFG-Unlock v1.3.3 (completion port, 2026-09-16)

https://github.com/dashdogy/RTX40MFG-Unlock , commit
`33b41835dc39c5d8ab1ef93efb2449be31139c09` (v1.3.3, MIT; source tree studied in
the gitignored `third_party_local/community/RTX40MFG-Unlock`). Ported pieces:

- the provider count/index validator patch from `source/native/ngx_mfg_gate.h`
  (pattern `84 d2 0f 84 03 01 00 00 be 05 00 00 00`, near-jz head `0f 84` →
  `eb 04`, whose entry condition gates the accepted generated-frame ceiling on
  the runtime-detected architecture). This is the documented mechanism behind
  upstream's v1.3.3 fix for "MFG getting stuck at 2x"; every count, index and
  profile-limit check after the branch is retained;
- the three-stage SHA-256 identity chain from `source/native/midpoint_fix.cpp`
  legacy profile: source fatbin (98408 B) `5A8E0284…`, decompressed PTX
  (99362 B) `46C05996…`, rebuilt fatbin (127200 B) `19FB3CD5…`, plus the
  sm_120/sm_89 entry field checks (28024/28017/90490 and 30384/30379/99362).
  Veyra reproduced the rebuilt-fatbin digest byte-for-byte from its own
  `runtime_local/nvidia/nvngx_dlssg.dll` 310.7 before porting; the digest is now
  a publication gate, so a build that does not reproduce the upstream-verified
  byte stream is refused and fully rolled back.

Veyra adaptations and additions: hard architecture gating (never applied on
Blackwell, so RTX 50 keeps its native path); the count/index site must match
uniquely, sit at an even address and branch to the audited `cmp r8d, 1` target
before any write; all edits happen in the mapped image only (disk file is never
touched, re-signed or renamed); read-back verification and full rollback when
the session ends. The rollback path re-opens page protection before restoring
bytes — negative testing during this port caught that the original Veyra
rollback path wrote into read-only pages and would have crashed the process on
a refused kernel fix. The upstream Streamline-side hardware flip-metering /
pacing workaround and per-title `nvidia_mfg_policy` ceilings are **not
applicable**: Veyra calls NGX directly and does not load Streamline, ReShade or
any RenoDX add-on.

Evidence on this machine (RTX 5070, structure and patch mechanics only):
`logs/fsr/dlssg-unlock-scan.log`, `logs/fsr/dlssg-unlock-applytest.log`;
delivery gate `logs/delivery/d0a266a1e549402ca26c2c8ec8d27022/result.json`.
Ada behaviour still has to be verified on RTX 40 hardware.

### dashdogy/RTX40MFG-Unlock v1.3.3 — RTX 30 (sm_86) Ampere path (2026-09-16)

Same upstream (MIT, commit `33b41835dc39c5d8ab1ef93efb2449be31139c09`),
`source/native/ampere_gpu.cpp` (`FindUniqueSm89Ptx`, `BuildAmpereSm86Fatbin`),
`ampere_cuda_program.cpp` (driver preflight intent) and `ampere_policy.h`
(sm_86 adapter contract), ported into
`include/veyra/ngx/AmpereMfgUnlock.h` / `src/ngx/AmpereMfgUnlock.cpp`.

The provider ships zero sm_86 targets; every DLSS-G fatbin only carries sm_89
PTX and sm_120 PTX/cubin. Veyra rebuilds all 69 fatbins of the audited 310.7
runtime as single-entry sm_86 PTX programs — 25 registration-table programs
(8 runs x 25 slots, 200 pointer fields), 38 `.rdata` neural-network programs
and 6 `.data` auxiliary programs (font/capture/clear) referenced through 44
RIP-relative `lea` sites — and republishes every reference plus the two
architecture compares (`0x1b0` -> `0x170`). The temporal program additionally
receives the same midpoint correction as the Ada path. Ada/Blackwell-only PTX
constructs (`.e4m3`, `.e5m2`, `wgmma.`, `tcgen05.`, `mmma.sp::ordered_metadata`,
`sm_90`, `sm_120`) are refused, matching the upstream refusal list.

Veyra additions: module identity is verified against the audited 310.7 build;
the rebuilt set must load through a private CUDA context (`cuModuleLoadDataEx`)
before any pointer is published; the allocation window is chosen so every
RIP-relative displacement stays encodable; every write is recorded and fully
restored on release; the on-disk DLL is never touched. The upstream native
cache (cubin) path is not used — dashdogy does not publish those kernels, and
the published PTX path is sufficient (verified 69/69 through the driver JIT).
Upstream explicitly labels its RTX 30 support "very early and experimental";
the same caveat applies here until real Ampere hardware validation.

Local evidence (RTX 5070, structure and mechanics only):
`veyra_dlssg_ampere_probe` — scan (runs=8, 200 slots, 25+38+6 fatbins, 44 lea,
2 gates, temporal unique), apply (applied=1, preflight=69/69, readBack=1,
restored=1), delivery gate
`logs/delivery/548a606d92484286806f272d4744d2a7/result.json`.

## Media Foundation encoder（系统硬件编码路径，移植自 FFmpeg mfenc.c）

Source: FFmpeg n9.0.1 `libavcodec/mfenc.c` 与 `libavcodec/mf_utils.c`
（LGPL-2.1-or-later；本地源码 `C:\veyra-deps\ffmpeg-ps5-slices-source`）。
`src/sink/MfVideoEncoder.cpp` 移植了其中被证明可用的部分：`MFTEnumEx` 硬件枚举与激活、
`MF_TRANSFORM_ASYNC_UNLOCK` 异步解锁、输入/输出媒体类型协商、`ICodecAPI`
（码率/低延迟/GOP/B 帧/质量）设置、事件驱动的 `ProcessInput`/`ProcessOutput` 循环、
`MFT_MESSAGE_COMMAND_DRAIN` 收尾，以及 `MF_MT_MPEG_SEQUENCE_HEADER`（SPS/PPS）
作为封装 extradata。

Veyra 的差异（非上游代码）：输入不是 AVFrame，而是 Veyra 自己 D3D12 图渲染出的 NV12
经 readback 打包；枚举结果按当前显卡厂商优选并对每个候选做完整契约协商（一台机器上可能
同时装着多家驱动的编码 MFT）；首个样本与 GOP 边界强制关键帧；码流直接交给 Veyra 的
MP4 封装路径。MFT 本身属于 Windows 与显卡驱动，不随包分发。FFmpeg 采用
LGPL-2.1-or-later，与 Veyra 的 GPLv3 兼容；上游作者归属见上。

## dav1d (1.3.0 AV1 playback)

FFmpeg dynamically links dav1d 1.5.4 from the pinned local vcpkg build. The portable package includes its complete aggregated copyright/license text in `licenses/DAV1D-COPYRIGHT.txt` and provenance in `licenses/DAV1D-SPDX.json`. The FFmpeg corresponding-source ZIP includes dav1d source and its vcpkg port. Upstream: https://code.videolan.org/videolan/dav1d . License set recorded by the build: Apache-2.0, BSD-2-Clause, ISC and MIT; retain all notices supplied with the source.

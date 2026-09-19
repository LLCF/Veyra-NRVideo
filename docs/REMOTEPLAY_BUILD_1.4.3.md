# Veyra 1.4.3 Test Build

Local test on branch codex/fg-backend-switch-20260919, following repair commit 8c9957b.
No v1.4.3 release tag or GitHub publication is implied.

Remote Play remains enabled. Chiaki is pinned to streetpea/chiaki-ng commit
0e16950165f06e5c3291537c2eeba6e852be7120; local patches are under scripts/remoteplay/patches.
The combined program is subject to Chiaki AGPL-3.0-only with OpenSSL exception;
Veyra source is GPLv3. Notices are included under licenses/remoteplay.

FFmpeg preserves the existing PS5 H.264 32-to-256 slice patch and dav1d build.
Dependency sources and notices are unchanged from 1.4.2; this local test does not replace
their corresponding source archives. Binary provenance is in licenses/FFMPEG-VEYRA-BUILD.json.

Build with VS2022 x64, CMake/Ninja and authorized local SDKs, using scripts/build-isolated.ps1
with DisplayVersion=1.4.3 and explicit E:/项目/Veyra build/temp directories. See BUILD.md.
No SDK, runtime, credentials or private media are added to source Git.

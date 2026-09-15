# AMD FSR SDK 2.3.0 本地落地记录（2026-09-16）

本轮为"AMD 补帧（AMD FSR Frame Generation）"与"FSR 超分"工作流做的第一步：把官方 SDK 2.3.0 落到本机并登记身份。**尚未接入代码，未发布。**

## 1. 来源与身份

| 项 | 值 |
| --- | --- |
| 来源 | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK |
| 标签/提交 | `v2.3.0` / `60f4ea81909200d8542eca14dccb2628b763a9a3`（2026-06-24，"AMD FSR SDK 2.3.0"） |
| 本地路径 | `third_party_local/amd/FidelityFX-SDK-2.3.0`（gitignore 内，不入源码 Git） |
| 体积 | 845 文件 / 约 239.2 MB（含 Samples、Tools 与签名的预编译 DLL） |
| 许可 | MIT 风格（`Kits/FidelityFX/docs/license.md`；头文件内嵌同一许可文本，Copyright (C) 2026 AMD） |

### 随 SDK 提供的签名预编译 DLL（`Kits/FidelityFX/signedbin/`）

| 文件 | 版本 | 大小 | SHA-256 |
| --- | --- | --- | --- |
| `amd_fidelityfx_framegeneration_dx12.dll` | **4.0.1.2740** | 38.2 MB | `02297BEEDD285E822D3A64F314CF00FAF378DCEC0EDC47FF0C4DD71B3A8C2F18` |
| `amd_fidelityfx_upscaler_dx12.dll` | **4.1.1.2740** | 27.4 MB | `D0DCCCC74A43C44BA435B7A369B456E0970D8A4464E4BD683119B374F2C9FB46` |
| `amd_fidelityfx_loader_dx12.dll` | 2.3.0.2740 | 约 10 KB | `E2D85AA05A9BD9ED8B38935FDF5199372CCA6F74C12015143BB6F945EE1608AA` |
| `amd_fidelityfx_loader_dx12.lib` | 2.3.0 | 导入库 | `01D76FF0AE9DB6DBB335A765991CC14CDA207D2597803D9DF983DC14AB504902` |
| `amd_fidelityfx_denoiser_dx12.dll` | 1.2.0.2740 | 13.1 MB | `48F1E5888BA6A0A3D59A98B9751E37C392B0F7B8C223D0082D5C1F40642879D3` |
| `amd_fidelityfx_radiancecache_dx12.dll` | 0.9.0.2740 | 0.8 MB | `256DB18D924C8CD38923D04E3ECD210695D3F0F796B240EAB9663AD4D54E31A0` |

说明：4.0.x 是 **ML 版帧生成**，4.1.x 是 **ML 版超分**，与用户"有新版就用新版"的要求一致；3.1.x 的非 ML 帧生成仍保留在本地 SDK 1.1.4（`ffx_frameinterpolation`）作为回退。

## 2. 接入形态（下一步施工用）

- SDK 2.x 起效果拆成独立 DLL，通过**provider API** 暴露；帧生成入口头文件：
  - `Kits/FidelityFX/framegeneration/fsr3/include/ffx_provider_fsr3framegeneration.h`
  - `Kits/FidelityFX/framegeneration/fsr3/include/ffx_provider_fsr3framegenerationswapchain.h`
  - `Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.h`（含 UiComposition / DebugPacing 变体）
- 超分入口：`Kits/FidelityFX/upscaler/...`（FSR Upscaling 4.1）与 SDK 1.1.4 的 `ffx_fsr2.h` / `ffx_fsr3upscaler.h`（FSR 2 / 3.1，可跨厂商）。
- 与本项目现有接法的关系：当前 `third_party_local/amd/FidelityFX-SDK/sdk`（1.1.4）是通过 **编译出的静态库** 接入的（`ffx_opticalflow_x64.lib` + `ffx_backend_dx12_x64.lib`，见 `CMakeLists.txt` 第 576 行起、`cmake/VeyraGpuDis.cmake`）。2.3.0 需要同样的"先构建 SDK，再链接 provider 与 backend 库"步骤，并且运行时加载上述签名 DLL。

## 3. 待办与边界

1. **构建路径勘察结论**：SDK 2.3.0 根目录没有 CMakeLists，官方构建方式是**每个效果一个 Visual Studio 解决方案**（`Samples/FidelityFX_<effect>/dx12/FidelityFX_<effect>_2022.sln`），依赖 vcpkg 与 Cauldron 框架——这是一套重量级样例构建，不适合直接搬进 Veyra。**但签名预编译 DLL 已随 SDK 提供**（见 §1 表格），所以下一步不是"重建 SDK"，而是：读 `Kits/FidelityFX/api` 与 `Kits/FidelityFX/framegeneration/fsr3` 的 provider/host 源码，评估把少量 provider 胶水源码直接编进 Veyra（或经 `amd_fidelityfx_loader_dx12.dll` 加载）的可行性，再按 `CMakeLists.txt` 里 1.1.4 静态库那种方式接入。只用其 DLL 与头文件、不修改任何 AMD 二进制。
2. **能力查询优先**：先在真实显卡上查询 FSR Frame Generation / Upscaling 4.x 是否可用（AMD 的支持列表按 GPU/驱动区分；NVIDIA 上大概率不可用），据此决定界面分档：AMD 卡开放 4.1/4.0.x，N 卡只给 FSR 2 / 3.1。
3. **发布范围**：把 `amd_fidelityfx_*_dx12.dll` 放进 Release 属于范围变更，需要用户单独授权，并在 `release-runtime-manifest.json` 与 `THIRD_PARTY_NOTICES.md` 中逐项登记（名称、版本、大小、SHA-256、来源、许可）。
4. **不改二进制**：所有 AMD DLL 原样使用，不做补丁、不重签名。

# 30 系（RTX 30）补帧决策（D 工作流，2026-09-16）

> 2026-09-16 复查更新：用户复核了 `sdli1995/dlssg_for_sm86` 的许可证显示，要求重新核查。
> 核查结果（`git ls-files` + 仓库目录实测，不是 GitHub 侧栏）：该仓库**没有任何 `.c/.cpp/.h`
> 源码文件，也没有 LICENSE 文件**；`README` 自述 GPLv3、GitHub 由此推断出 GPL-3.0 标签，
> 但仓库本体只有 18 个 DLL（`version.dll`、`dxgi.dll` 等命名）+ ini + 文档。它是**伪装成
> 系统 DLL 的代理加载器**：导出表只有 `GetFileVersionInfoW` 一类转发函数加一个
> `DlssgProxy_Name` 标记，运行时把 DLSSG 调用重定向到 FSR3 补帧。没有源码就没有可搬运对象，
> 随包分发二进制则等于分发无许可证文本的第三方 DLL——两条都不成立。

## 结论

**不移植 `sdli1995/dlssg_for_sm86`，也不随包分发它的 DLL。** 理由不是"做不到"，是两条硬约束：

1. 该仓库**没有源码、没有 LICENSE 文件**（README 自述 GPLv3 但没有随附许可证文本）。
   项目规则对无许可证材料的原话是"不得复制代码"；二进制同样不作为可搬运对象。
2. 它的形态是**替换式代理**，本质是把 DLSSG 调用转成 FSR3 帧生成。也就是说：它提供的能力
   我们自己**已经有了一个更干净的等价物**——见下。

## dashdogy/RTX40MFG-Unlock 的 30 系实验路径（2026-09-16 评估）

dashdogy v1.3.3 源码中含有 30 系实验后端（`ampere_backend.cpp`、`ampere_gpu.cpp`、
`ampere_cuda_program.cpp`、`ampere_native_cache.cpp` 等，MIT）。但 `BUILD.md` 明确写明：
它需要**单独准备的 "validated SM86 kernel cache"**（`native-cache/`、`native-cache-3109/`、
`all-provider-layouts/` 三套 fatbin 缓存 + manifest），并原话标注 *"Kernel caches are
separately prepared build inputs and are not included in this source tree. A source checkout
alone cannot reproduce the DLL without them."* README 同时标注 30 系支持 *"very early and
experimental"*、可能完全不工作。这些 kernel cache 是构建输入的二进制 blob，不在公开源码里，
我们既不伪造也不向第三方索取未公开材料——所以**维持搁置**：30 系不接原生 DLSS MFG 路径。

同类里许可证明确的是 `Nukem9/dlssg-to-fsr3`（GPL-3.0）。如果将来要做"代理式"方案，
应当以它为上游（GPL-3.0 与 Veyra 兼容），而不是用无源码的 sm86 包。

## 30 系用户现在的 2X 路径

Veyra 已经落地了 AMD FSR 帧生成（E 工作流，本机 RTX 5070 实测 2X 可用）：
它是 D3D12 上厂商无关的 FidelityFX 效果，**不依赖 Blackwell**，因此 30 系用户
在软件里直接选「AMD FSR 帧生成 · 2X」即可获得补帧，不需要任何 NVIDIA DLL 代理，
也不需要绕过签名。

证据（本机）：`docs/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md`（1080p 226/222、4K 559/555、
同帧对照平均绝对差 0.31/255、生成帧确实位于时间中点）。

## 未验证 / 待办

- **没有在 RTX 30 实机上验证过 FSR 帧生成**（本机只有 5070）。提供方在 5070 上正常枚举与运行，
  但"30 系也能跑"目前是**推断**，不是证据；必须在一张 3060/3070 上跑
  `veyra.exe --fg-fsr --smoke-seconds 10 <视频>` 才能对外宣称。
- 若 30 系实机发现 FSR FG 不可用，再评估以 `Nukem9/dlssg-to-fsr3`（GPL-3.0）为上游做代理式方案；
  那需要额外的发布范围授权（它会替换游戏/系统目录里的 `nvngx_dlssg.dll` 语义，
  与 Veyra 现在"进程内不改磁盘"的路线不同）。
- RTX 20 系（SM75）不在本次范围。

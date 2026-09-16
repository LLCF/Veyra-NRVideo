# Veyra 1.3.2beta 测试包说明（隔离分支构建，未发布）

**这是给用户实机测试用的 beta 包，不是正式发布。** 构建来源：分支
`codex/capture-decode-latency-20260916`（未合并 main、未推送、未创建 Release）。
本包在 1.3.1beta 全部内容之上，重点是**采集（MPEG/压缩格式）链路重构**。

## 1.3.2beta2 修复（收到 beta1 的人必看）

**beta1 有一个严重影响原生采集格式（YUY2/NV12/RGB 系）的回归**：解码 worker 的帧池改造
错误地套用到了非压缩路径，`read()` 把原生路径的 `pendingFrame` 置空，下一个驱动回调被判定为
采集错误 → 引擎反复重连（约每 1 秒一轮），用户看到的是"采集进去只有 2 帧"。
压缩格式（MJPEG/H.264/HEVC）不受影响，这也是它没在 beta1 的 MJPEG 测试里暴露的原因。
beta2 已修复，并在真机对**四条路径各跑 2 分钟**：YUY2 1080p60 7664 帧 / YUY2 4K18 2150 帧 /
MJPEG 1080p60 7176 帧 / MJPEG 4K18 2150 帧，全部 0 丢帧、无重连。
新增 `scripts/acceptance/capture-paths-smoke.ps1`：一条命令同时验证原生与压缩两条路径。

## 采集链路（本次重点）

- **压缩格式不再走系统解码器**：MJPEG/H.264/HEVC/AV1/VP9 由 `ConnectDirect` 直接送入我们的
  压缩 sink，解码由自有后端完成，不再经过"系统解码器 → 强制 RGB32 → SampleGrabber"这条路径。
- **MJPEG 自有软解 worker**：DirectShow 回调只拷贝压缩 payload（数百 KB），独立线程解码为
  full-range NV12；有界队列丢旧保新。本机 2 分钟实测：1080p60 与 4K18 均 0 丢帧、
  `errors=0 queueDrops=0`；图侧每帧 CPU P95 1080p 1.987→0.36ms、4K 6.218→0.92ms。
- **H.264/HEVC/AV1/VP9 走 D3D12VA**（共享 Veyra 设备），纹理直入图；首帧不可导入自动回退软解。
  本机采集卡没有这两种格式，合成码流验证：H.264 硬解/软解各 900 帧、HEVC 各 252 帧，帧数一致。
- **三级回退**：D3D12VA → 软解 → RGB32 兼容路径；任一级失败不断流，日志写明当前后端。
  采集面板的格式列表里压缩格式仍排在原生格式之后（延迟档位标注不变）。
- **延迟口径注意**：`[capture-timing]` 的 `callback→Present` / `readAgeMs` 现在包含我们自己的解码
  （旧路径的解码发生在系统解码器里、不在窗口内），因此这两个数字与旧版本**不可直接比较**；
  端到端延迟请用相机法或与 OBS/PotPlayer 同源对比。

## 颜色

- 采集颜色 A/B 工具（`veyra_capture_color_probe.exe <采集路径> <目录> [--legacy-rgb]`）修复了
  双平面格式的越界崩溃，现在新/旧两条路径都能输出对照图。**静止画面的逐像素验收还没做**，
  测试时若发现颜色异常请附上两条命令的输出。
- H.264/HEVC 的 in-band 参数集与 `MPEG2VIDEOINFO`（Annex-B / avcC / hvcC / 长度前缀四种形态）
  都做了解析；10bit 压缩输入目前会降位到 8bit（日志会写警告），HDR 压缩采集未实现。

## 继承自 1.3.1beta（不变）

- DLSS 6X（50 系）/ 40 系 MFG 解锁 / XeSS MFG（2X-4X）/ AMD FSR 帧生成与超分；
- 杜比/DTS 位流解码兜底（AC-3/E-AC-3/TrueHD/DTS/DTS-HD）与采集音频手动选择；
- 原生采集格式（YUY2/NV12/RGB 系）的 N1/N3/N4 优化（GPU 解包、格式排序、缓冲协商）。

## 测试重点

| 场景 | 建议 |
| --- | --- |
| MJPEG 采集卡（1080p60 / 4K） | 关闭全部增强，与 OBS/PotPlayer 对比延迟体感；看 `[capture-timing]` 的 `readAgeMs` 与 `captureDropped` |
| H.264/HEVC 采集卡 | 确认能出画面；日志应有 `compressed ConnectDirect codec=h264/hevc ... hr=0x0` 与 `backend=d3d12va`；若显示 `backend=software` 或回退 RGB32，请把日志发回 |
| 颜色 | 静止画面下跑 `--legacy-rgb` A/B（见上） |
| 原生格式卡 | 确认无回归（应与 1.3.1beta 一致） |

## 已知边界

- H.264/HEVC 未在本机真卡验证（本机卡只有 YUY2/MJPEG）；AV1/VP9 采集未验证。
- 端到端光子延迟未测；4K18 的 `readAgeMs` 与上一内部版本存在 0.4–3ms 无法定性的差异。
- 帧池固定 4 帧（1080p 约 12MB / 4K 约 50MB 系统内存）。

## 包内容

- `Veyra.exe`、FFmpeg 运行库、VC 运行库、`shaders/`
- `runtime/experimental/`（NVIDIA 组件）、`runtime_local/intel/experimental/`（XeSS）、
  `runtime_local/amd/fidelityfx/`（AMD FidelityFX SDK 2.3.0，MIT，AMD 签名）
- 许可证、第三方 notices、`release-runtime-manifest.json`

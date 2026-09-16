# 采集解码与延迟：最终架构实施方案（2026-09-16）

状态：**方案文本**。包含两部分：①压缩格式（MJPEG/H.264/HEVC）的解码架构；②原生格式
（YUY2/NV12/RGB…）的 N1–N4 优化。两者合起来的目标是"低延迟 + 顶级效果 + N/A/I 三家全覆盖"。

> 本文档在 main 工作区重建（原版位于隔离分支 `codex/framegen-fsr-dolby-20260916` 提交
> `1028ddb`），并追加第 8 章"原生链路 N1–N4"。

## 1. 实测依据（本机 RTX 5070 + AMD 核显 + USB3 采集卡）

| 事实 | 数字 | 来源 |
| --- | --- | --- |
| 我们软件段（1080p60 原生、无增强） | **2.640 ms**（=0.16 帧） | 实卡 `[capture-timing]` |
| 我们软件段（1080p60 MJPEG、无增强） | 4.121 ms（CPU 2.16ms/帧） | 实卡 |
| 4K（卡上限 18fps）MJPEG 每帧 CPU | 7.04 ms | 实卡 |
| 4K MJPEG 软解（多线程无效，熵解码串行） | 7.2 ms/帧（单线程 8.0） | ffmpeg benchmark |
| 4K MJPEG 用 NVDEC（帧留 GPU） | **2.97 ms/帧** | `-hwaccel cuda -hwaccel_output_format cuda` |
| 4K MJPEG 用 D3D11VA | 7.34 ms/帧（=软解回退） | 同左 |
| 增强代价（60fps 实卡） | NR+SR +10.5ms；再开补帧 +4.4ms | 实卡 |
| 卡侧 A/V 相对偏移 | 视频比音频晚 ≈46ms（YUY2 与 MJPEG 相同） | `[live-audio-sync] skewMs` |
| D3D12 解码能力探针 | N 卡：H264/HEVC/HEVC10/AV1/VP9 + **MJPEG_420**；A 卡核显：仅 H264/HEVC/AV1/VP9，**MJPEG 全不支持** | `veyra_decode_profile_probe.exe` |
| OBS 同卡配置 | `format: YUY2`、`buffering: disabled`、`hardware decode` 开关 | OBS 日志 |
| 4K 采集回调：YUY2 行拷贝（16.6MB） | 0.89 ms/帧 | 微基准（本机） |
| 4K 采集回调：BGR24→BGR0 逐像素 | **3.88 ms/帧** | 微基准 |
| 4K 采集回调：UYVY→YUY2 逐像素 | **2.22 ms/帧** | 微基准 |
| 4K 采集回调：RGB565→BGR0 逐像素 | **8.41 ms/帧** | 微基准 |
| 4K 对照：单次 16.6MB memcpy | 0.70 ms | 微基准 |

## 2. 最终架构（压缩格式）

```
采集卡 → UVC 驱动样本
  ├─ 原生格式（§8 处理）
  ├─ H.264 / HEVC / AV1（含 10bit） → FFmpeg D3D12VA（复用我们的 D3D12 设备/队列）
  │        → AV_PIX_FMT_D3D12 纹理 → 图的硬件入口（已存在，文件播放就在用）
  └─ MJPEG → 帧级并行软解（每帧独立）→ NV12 → 图的软件入口
                                   ↓
        统一：显式颜色合同（BT.601/709/2020、full/limited、PQ/HLG）→ 增强 → 呈现(vsync=0+tearing)
```

**移除**：系统解码器 → 强制 RGB32 → SampleGrabber → NullRenderer（保留为诊断开关）。

可行的三条依据：①图已有 `AV_PIX_FMT_D3D12` 硬件入口；②FFmpeg 的 D3D12VA 已包着我们自己的
D3D12 设备（`FFmpegVideoDecoder::openD3D12VA`）；③构建里已有 H264/HEVC/AV1 的 D3D12VA 封装、
没有 MJPEG 的（正好对应两条实现，不需要重建 FFmpeg）。

## 3. 压缩链路模块改动

| 模块 | 改什么 |
| --- | --- |
| `CaptureCardSource.cpp/.h` | 格式分类；压缩格式改 `ConnectDirect(设备pin→我们的 sink)`（不再 RenderStream+RGB32）；**视频 pin 缓冲协商**；解析 H.264/HEVC 的 sequence header 作 extradata |
| `NativeCaptureSink.cpp/.h` | 接受压缩媒体类型，把 payload+时间戳交上层队列（压缩样本很小，拷一份即可） |
| 新增 `CaptureDecodeQueue` | 有界队列 + 线程池；下游慢时丢旧帧（与现有 mailbox 语义一致） |
| 新增 `CaptureDecoder` | 后端选择与三级回退：D3D12VA → 并行软解 → 单线程软解 |
| `FFmpegVideoDecoder.*` | 抽出可复用的 D3D12VA 会话（设备/队列/池参数） |
| `EngineController.cpp` | 采集分支接新入口；`[capture-timing]` 增加 `backend/decodeMs/queueDepth` |
| `CapturePanel` / `SettingsWindow` | 格式排序标注（见 N3）；设备缓冲档位（见 N4） |

## 4. 压缩链路施工顺序

①能力探测与帧契约 → ②压缩样本直连 → ③H.264/HEVC/AV1 硬解 → ④MJPEG 并行软解
→ ⑤缓冲协商+UI（与 N4 合并）→ ⑥原生拷贝合并（与 N2 合并）→ ⑦清理默认路径 + 门禁 + 真机对比。

## 5. 压缩链路验收指标

| 指标 | 目标 |
| --- | --- |
| 1080p60 MJPEG 全链路 callback→Present | ≤3.5 ms（现状 4.12） |
| 4K MJPEG 每帧 CPU | ≤3 ms（现状 7.0+） |
| 4K60 MJPEG 推算 | 每帧 CPU ≤3ms 且 `readAgeMs` ≤1 帧、`callbackFps` ≥名义 98%（不积压） |
| H.264/HEVC | 解码在 GPU：每帧 CPU ≤1 ms，颜色与软解逐像素差 ≤2 code |
| 回退 | 任一级失败自动降级，不断流；日志写明后端 |
| 门禁 | 合同 180 项、delivery 短测、预设/UI 合同全过 |

## 6. 压缩链路风险与对策

| 风险 | 对策 |
| --- | --- |
| AMD 要求 reference-only allocations | 帧池按该约束建；失败回退并行软解并记日志 |
| 卡的 MJPEG 可能是 4:2:2（N 卡只硬解 4:2:0） | MJPEG 一律走并行软解；确认 4:2:0 后再考虑启用硬解 |
| UVC 的 H.264 sequence header 形式不一 | 同时支持 `MPEG2VIDEOINFO` 与 in-band SPS/PPS；解析失败回退软解 |
| 并行软解与 NR/SR 抢 CPU | 线程数 = min(4, 核数-2)，占用可见 |
| 驱动/码流兼容失败 | 三级回退（D3D12VA → 并行软解 → 单线程软解） |
| 缓冲压太小丢帧 | 档位制（自动/最小/驱动默认），默认自动，丢帧计数可见 |

## 7. 明确不做（边界）

- 不动导出链路（NVENC/MF、码率、CFR 逻辑）。
- 不动 NR/SR/补帧算法与调度。
- 不动文件播放路径（D3D12VA 只做"抽出来复用"）。
- 不引入新的第三方二进制依赖。
- 不宣称"整体延迟 X ms"：卡内部、DWM、显示器三段不可控，只能用真机端到端测量下结论。

## 8. 原生链路（未压缩格式）优化：N1–N4 全部实施

原生格式不走解码器，但当前在**采集回调线程里**做两件重活，且有 5 种打包格式是**逐像素 CPU
转换**（基线见 §1 微基准）。

### N1：逐像素转换从 CPU 挪到 GPU

- **改动**：`copyCaptureSample` 对 UYVY/YVYU/BGR24/RGB555/RGB565 只做"按行原始字节拷贝"；
  `EnhanceGraph` 入口接受这些 packed 布局；着色器用**一个带 packing 常量的统一变体**，
  保留现有 `Yuy2ToLinear`/`RgbToLinear` 行为。
- **验收**：
  - 4K RGB565 采集回调每帧 **≤1.2 ms**（现 8.41）；4K BGR24 **≤1.5 ms**（现 3.88）；
    4K UYVY **≤1.2 ms**（现 2.22）；4K YUY2 不回退 ≤1.0 ms；
  - 颜色：各格式与旧 CPU 路径逐像素比对，平均误差 ≤1、最大 ≤2 code（合成色卡 + 真实信号各一次）；
  - 720p/1080p/4K 各连续 60 秒：`captureDropped=0`、无花屏；
  - 提供"切回 CPU 转换"的诊断开关。

### N2：两次拷贝合并成一次

- **改动**：`EnhanceGraph` 暴露 `acquireIngressBuffer(parity)` / `commitIngressFrame(parity,pitch,format,pts)`；
  `NativeCaptureSink` 直接写进图的上传缓冲；无图上下文时保留旧信箱路径。
- **验收**：
  - 4K YUY2 全链路每帧 CPU **≤1.0 ms**（现 1.186）；4K RGB32 **≤2.0 ms**（现约 2.4）；
  - 写入前等待该 parity 的 fence；debug 断言 + 日志证明从不写"正在被 GPU 读"的缓冲；
  - 连续 5 分钟 4K60 + 3 次分辨率切换 + 2 次暂停/恢复：无撕裂/花屏，`captureDropped` 不高于旧路径；
  - 失败自动回退旧路径并记日志。

### N3：格式排序与延迟标注

- **改动**：推荐顺序 `NV12/P010 < YUY2 < RGB24 < RGB32 < UYVY/YVYU/BGR24/RGB555/RGB565`，
  压缩格式永远排在原生之后；每项标注"延迟档位：低/中/高"与"是否需要 CPU 拆包"；选到高成本模式给一次性提示。
- **验收**：
  - 自动化断言：同一设备同时提供 YUY2 与 RGB565 时，推荐列表把 YUY2 排在前面；
  - **用户已记住的格式优先于推荐**（不覆盖用户选择）；
  - 4K60 + 压缩格式选择时出现提示（可关闭）。

### N4：视频 pin 缓冲协商 + 界面档位

- **改动**：两条直连路径在 `ConnectDirect` 之前对视频输出 pin 调
  `IAMBufferNegotiation::SuggestAllocatorProperties`（advisory）；日志记录 requested/actual
  `cBuffers/cbBuffer`；设置项"设备缓冲：自动 / 最小 / 驱动默认"（默认自动）。
- **验收**：
  - 日志出现 actual `cBuffers/cbBuffer`（现在完全没有）；
  - 支持协商的卡上 `cBuffers` 从驱动默认（常见 3）降到 **≤2**，并给出 `readAgeMs` 变化；
  - 1080p60 YUY2 的 `readAgeMs` 目标 **≤0.6 ms**（现约 1.0）；
  - 驱动不理会时写"协商未生效"，**不得计入收益**；
  - `captureDropped` 超旧路径 1.5 倍时自动回到"自动"档。

### 8.1 原生链路施工顺序

N4 → N3 → N1 → N2（N4/N3 便宜且立刻可测；N1 要动 shader 与入口格式；N2 风险最高放最后）。

### 8.2 原生链路总验收（硬性）

1. **延迟必须比现在低**：同一台机、同一张卡、同一路信号，改前/改后各跑 3 次 60 秒，
   下列指标**至少两项改善 ≥5% 且无一项回退 >3%**：`callback→Present P95`、每帧采集回调 CPU、
   `readAgeMs`、`captureDropped`。若某张卡四项全无变化（例如纯 YUY2 且驱动不响应协商），
   **如实报告"该卡无收益"**，不得把未生效项写进收益。
2. **逐像素格式必须显著改善**：RGB565/UYVY/BGR24 的采集回调每帧成本相对 §1 基线
   **至少下降 50%**（目标见 N1）。
3. **颜色零回归**：N1 各格式与旧路径逐像素差 ≤2 code；P010(HDR) 路径不受影响。
4. **稳定性**：4K60 连续 30 分钟（含 3 次分辨率切换、2 次暂停/恢复）无花屏、无丢帧尖峰、
   RSS 增长 ≤50 MB、无句柄泄漏。
5. **门禁**：合同 180 项、`delivery.ps1` 短测、预设/UI 合同全过；采集/导出/图片路径无回归。
6. **可回退**：N1、N2 各自独立开关，出问题无需改版本即可切回旧路径。

### 8.3 原生链路真机要求

这张 ¥30 卡只提供 YUY2/MJPEG，**没有 UYVY/BGR24/RGB555/RGB565** → N1 的正收益本机无法取证。
两条路径：① 找一张能输出这些格式的卡（很多 USB3.0 卡低带宽模式就是 UYVY/RGB565）跑改前/改后；
② 拿不到卡时用合成样本喂 `copyCaptureSample` 与着色器做逐像素比对 + 微基准，并在报告里明确
标注"未在真卡验证"，不得写成已验收。

## 9. 三家显卡兼容（实测 + 待测）

| 能力 | NVIDIA | AMD | Intel |
| --- | --- | --- | --- |
| H264/HEVC/AV1 D3D12 硬解 | ✅ 实测 tier2 | ✅ 实测 tier2（ref-only alloc） | 待探针确认 |
| MJPEG D3D12 硬解 | ✅ 仅 4:2:0 | ❌ | 不押注 |
| MJPEG 自研并行软解 | ✅ | ✅ | ✅ |
| 原生格式 N1–N4 | ✅ | ✅ | ✅ |

## 10. 必须真机验证

1. Intel 显卡跑 `veyra_decode_profile_probe.exe`（只读），确认 D3D12 解码支持面；
2. 一张真 4K30/4K60 卡：MJPEG 采样格式（4:2:0/4:2:2）、改前/改后每帧成本、掉帧率；
3. 能输出 UYVY/BGR24/RGB565 的卡：N1 的改前/改后（否则只能合成样本验证）；
4. 相机端到端（240fps 拍主机画面 + 显示器）：改前/改后 + OBS/PotPlayer 同条件对比。

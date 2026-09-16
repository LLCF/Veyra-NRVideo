# 采集解码与延迟：最终架构实施方案（2026-09-16）

状态：**方案文本，尚未施工**。目标是"低延迟 + 顶级效果 + N/A/I 三家都覆盖"一条架构，不分期、不并行多套过渡形态。

## 1. 实测依据（本机 RTX 5070 + AMD 核显 + USB3 采集卡）

| 事实 | 数字 | 来源 |
| --- | --- | --- |
| 我们软件段（1080p60 原生、无增强） | **2.640 ms**（=0.16 帧） | `[capture-timing]` 实卡 |
| 我们软件段（1080p60 MJPEG、无增强） | 4.121 ms（CPU 2.16ms/帧） | 同上 |
| 4K MJPEG 软解每帧 | **7.2 ms**（单线程 8.0，多线程无效→熵解码串行） | ffmpeg benchmark |
| 4K MJPEG 用 NVDEC（帧留 GPU） | **2.97 ms/帧** | ffmpeg `-hwaccel cuda -hwaccel_output_format cuda` |
| 4K MJPEG 用 D3D11VA | 7.34 ms/帧（=软解回退） | 同上，说明 D3D11VA 解 MJPEG 不可靠 |
| 增强代价 | NR+SR +10.5ms；再开补帧 +4.4ms | 实卡 60fps |
| 卡侧 A/V 相对偏移 | 视频比音频晚 ≈46ms（YUY2 与 MJPEG 一致） | `[live-audio-sync] skewMs` |
| D3D12 解码能力探针 | N 卡：H264/HEVC/HEVC10/AV1/VP9 + **MJPEG_420** 支持；**A 卡核显：仅 H264/HEVC/AV1/VP9，MJPEG 完全不支持** | `veyra_decode_profile_probe.exe` |
| OBS 同卡配置 | `buffering: disabled`、`hardware decode` 开关 | OBS 日志 |

结论：**H.264/HEVC/AV1 可以靠系统硬解三家通吃；MJPEG 押不住硬解，必须我们自己解。**

## 2. 最终架构

```
采集卡 → UVC 驱动样本
  ├─ 原生格式 YUY2/NV12/P010/RGB24/RGB32 → 现有原生路径（本次只做"两次拷贝合并成一次"）
  ├─ H.264 / HEVC / AV1（含 10bit） → FFmpeg D3D12VA 解码（复用我们已有的 D3D12 设备/队列）
  │        → 解出 AV_PIX_FMT_D3D12 纹理 → 图的硬件入口（已存在，文件播放就在用）
  └─ MJPEG → 自研"帧级并行软解"（MJPEG 每帧独立，可多帧并行）
           → NV12 帧 → 图现有的软件入口
                                   ↓
        统一：显式颜色合同（BT.601/709/2020、full/limited、PQ/HLG）
              → GPU 色度/转换 → NR/超分/补帧 → 呈现（vsync=0 + tearing）
```

**彻底移除**：系统解码器 → 强制 RGB32 → SampleGrabber → NullRenderer 这条兼容路径（保留为诊断开关，不再走默认）。

### 为什么这条可行（不是画饼）

1. **图已经有硬件纹理入口**：`EnhanceGraph` 支持 `AV_PIX_FMT_D3D12` 输入（`hardwareInputFrames_`），文件播放的 D3D12VA 就是走这条路 —— 采集压缩流复用同一入口，**不需要新的零拷贝管线**。
2. **FFmpeg 已包着我们的 D3D12 设备**：`FFmpegVideoDecoder::openD3D12VA()` 用 `AVD3D12VADeviceContext` 包我们的 device/queue；采集侧复用同一套。
3. **构建里已有需要的解码封装**：`CONFIG_H264/HEVC/AV1/VP9_D3D12VA_HWACCEL=YES`；**没有** MJPEG 的封装 → 正好对应"两条实现"。
4. **能力探针已就绪**，可在任意机器上确认支持面（Intel 待跑）。

## 3. 模块级改动清单

| 模块 | 改什么 | 关键点 |
| --- | --- | --- |
| `src/source/CaptureCardSource.cpp/.h` | ①格式分类：原生 / 压缩两族；②压缩格式改为 `ConnectDirect(设备pin → 我们的 sink)`，不再 `RenderStream`+RGB32；③对**视频**输出 pin 调 `IAMBufferNegotiation::SuggestAllocatorProperties`（现在只有音频做）；④解析 H.264/HEVC 媒体类型里的 sequence header 作为 `extradata` | 压缩类型不再要求 `captureMediaLayout`；记录实际 `cBuffers/cbBuffer` |
| `src/source/NativeCaptureSink.cpp/.h` | 接受压缩媒体类型（不解析像素布局），把样本 payload + 时间戳交给上层队列 | 保持"借样+立即归还"，压缩样本必须**拷贝一份**（大小只有几十~几百 KB，远小于一帧未压缩数据） |
| 新增 `src/source/CaptureDecodeQueue.cpp/.h` | 有界队列 + 工作线程池：压缩样本 → 解码 → 输出帧 | MJPEG 池化并行（2–4 路）；H.264/HEVC/AV1 走 FFmpeg D3D12VA（单路或 2 路）；下游慢时**丢旧帧**（与现有 mailbox 语义一致） |
| 新增 `src/source/CaptureDecoder.cpp/.h` | 后端选择 + 回退：`D3D12VA(格式支持)→并行软解→单线程软解`；MJPEG 直接走并行软解 | 能力判定用 `ID3D12VideoDevice::CheckFeatureSupport`（复用探针逻辑，抽成 `decodeProfilesAvailable()`） |
| `src/media/FFmpegVideoDecoder.*` | 抽出可复用的"D3D12VA 会话"（设备/队列/池参数），供采集解码复用 | 不改文件播放行为；`extra_hw_frames` 按最坏延迟给 2–3 |
| `src/engine/EngineController.cpp` | 采集分支接入新入口；压缩路径的帧直接是 D3D12 纹理，原生路径不变；新增诊断字段（decodeBackend、每帧解码耗时、队列深度） | 现有 `[capture-timing]` 增加 `decodeMs`、`queueDepth`、`backend=` |
| `apps/veyra/ui/CapturePanel.cpp` | 格式列表按"延迟成本"排序并标注：原生（最低）/卡内编码（+1~3 帧）/需系统解码（更高）；显示"需 CPU 解码"提示 | 4K60 且选到压缩格式时给一次建议 |
| `apps/veyra/SettingsWindow.cpp` | 新增"设备缓冲：自动 / 最小 / 驱动默认"档位 | 默认=自动（按分辨率选择 cBuffers） |
| `tests/integration/CaptureSourceTests.cpp` | 新增：压缩格式直连成功、后端选择与回退、缓冲协商数值、并行软解吞吐 | 真机可跳过（无卡时按现有约定） |

## 4. 施工顺序（单一序列，不是分期方案）

1. **能力与契约**：抽出 `decodeProfilesAvailable()`（探针逻辑）+ 定义"解码输出帧"契约（D3D12 纹理 或 NV12 CPU 帧）。
2. **压缩样本直连**：压缩格式改 `ConnectDirect`，`NativeCaptureSink` 接受压缩类型并拷贝 payload；日志记录 subtype 与字节数。
3. **H.264/HEVC/AV1 后端**：复用 FFmpeg D3D12VA，解出的纹理走图现有硬件入口；先保证"能出画面、颜色正确"。
4. **MJPEG 并行软解**：线程池 + 有界队列 + 丢旧帧；输出 NV12 走现有软件入口。
5. **缓冲协商 + UI**：视频 pin 协商、缓冲档位、格式排序标注。
6. **原生格式拷贝合并**：sink 直接写进图中转缓冲（省一次整帧拷贝）。
7. **清理与验收**：默认不再走 RGB32 兼容路径；跑完整门禁 + 真机对比。

## 5. 验收指标（可量化，达不到不算完成）

**本机可测（RTX 5070 + 这张 USB3 卡）**

| 指标 | 目标 |
| --- | --- |
| 1080p60 原生，无增强 | callback→Present ≤ **3 ms**（现状 2.64，不许回退） |
| 1080p60 MJPEG | callback→Present ≤ **3.5 ms**（现状 4.12） |
| 4K（尽可能高帧率）MJPEG | 每帧 CPU ≤ **3 ms**（现状 7.0+） |
| 4K MJPEG 4K60 推算 | 每帧 CPU ≤ 3 ms 且 `readAgeMs` ≤ 1 帧、`callbackFps` ≥ 名义 98%（**不出现积压**） |
| 颜色一致性 | 同一画面：压缩路径输出与"软解参考"逐像素差 ≤ 阈值（RGB32 路径作对照） |
| 门禁 | 修复合同 180 项、delivery 短测、预设/UI 合同全过；采集/导出既有行为不回归 |

**必须真机（我这边替代不了）**

1. Intel 显卡跑探针（`veyra_decode_profile_probe.exe`）确认 H264/HEVC/AV1 的 D3D12 支持与 MJPEG 情况；
2. 一张真 4K30/4K60 卡上：MJPEG 采样格式（4:2:0 还是 4:2:2）、改前/改后每帧成本、掉帧率；
3. 端到端（相机 240fps 拍主机画面+显示器）改前/改后 + OBS/PotPlayer 同条件对比。

## 6. 风险与对策

| 风险 | 对策 |
| --- | --- |
| AMD 要求 **reference-only allocations**（探针 flags 已给出） | 解码帧池按该约束创建；初始化失败时记明确日志并回退并行软解 |
| 卡的 MJPEG 可能是 **4:2:2**（N 卡只硬解 4:2:0） | 不押注硬解：MJPEG 一律走并行软解；若确认是 4:2:0 再启用 D3D12 MJPEG 加速 |
| UVC 的 H.264 媒体类型 sequence header 位置/形式不一 | 同时支持 `MPEG2VIDEOINFO`(SPS/PPS) 与采样内 in-band SPS/PPS；解析失败→回退软解并记录 |
| 并行软解和 NR/SR 抢 CPU | 线程数默认 = min(4, 核数-2)，可在诊断里看到占用；GPU 侧业务不受影响 |
| 驱动/码流兼容导致解码失败 | 三级回退（D3D12VA → 并行软解 → 单线程软解），任何一级失败都不断流 |
| 缓冲压到最小引发丢帧 | 档位制（自动/最小/驱动默认），默认自动；丢帧计入 `captureDropped` 并在 UI 显示 |
| 颜色回归 | 引入"软解参考帧"作为对照测试；颜色合同沿用现有显式解析，不新增隐式转换 |

## 7. 明确不做（边界）

- 不动导出链路（NVENC/MF、码率、CFR 逻辑）。
- 不动 NR/SR/补帧算法与其调度。
- 不动文件播放路径（D3D12VA 文件解码只做"抽出来复用"，不改变其行为）。
- 不引入新的第三方二进制依赖（复用已批准的 FFmpeg 构建能力；若将来需要 NVDEC 直连再单独审批）。
- 不宣称"整体延迟 X ms"：卡内部、DWM、显示器三段不在我们控制内，只能靠真机端到端测量给结论。

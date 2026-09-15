# FSR 超分（F 工作流）：证据、结论、接入点（隔离分支）

**这是计划，不是已完成功能。** 本轮只做了可行性验证与接入点勘察，产品里还没有 FSR 超分。
分支 `codex/framegen-fsr-dolby-20260916`，未合并 main、未推送、未发布。

## 1. 用户问题的直接回答

> 「FSR超分能不能也加进来？N卡能用吗」

**能，而且本机已经用证据证明了**（RTX 5070 / 616.56）：

| 问题 | 实测结果 | 日志 |
| --- | --- | --- |
| N 卡能跑 FSR 超分吗 | 能。`CreateContext(upscale)` OK，`Dispatch(1280×720 → 2560×1440)` OK，回读是真图：均值 127.33、动态范围 0–255、15 个灰度级、棋盘相位正确 | `logs/fsr/upscale-probe.log` |
| N 卡上是哪个 FSR 版本 | **只有 3.1.5 与 2.3.4 被枚举**；4.x(ML) 提供方在 NVIDIA 上根本不出现 | 同上 |
| 所以 N 卡分档应该怎么写 | N 卡 = FSR 3.1.x（超分），4.1 ML 只能留给 AMD 卡实测后再开 | — |

工具：`tools/fsr_upscale_probe`（CMake 目标 `veyra_fsr_upscale_probe`），做法是上传
64 像素棋盘 + 水平渐变 → FSR 放大 2 倍 → 回读校验内容（不是"跑通不报错"就算过）。

## 2. 与现有图结构的关系（接入点）

Veyra 的 SR 阶段在 `EnhanceGraph` 的 `runSr()`，现在只有两个分支：

1. `videoSrBackend_`：RTX 视频超分（`videoSrQuality` 1–4），输入 `videoSrInput_`（SR 尺寸 RGBA8）→
   `videoSrOutput_`（工作尺寸 RGBA8）→ 回填 `workRgba_`；
2. `srBackend_`：DLSS SR（`videoSrQuality=0`），输入 `srcRgba_` + `nrZeroDepth_` +
   `baseFlow_`（工作分辨率像素运动）→ 直接写 `workRgba_`；
3. 都没有时走 `blitPass_` 直通缩放。

FSR 超分应作为**第 3 个后端**接入，语义与 DLSS SR 分支最接近，需要的输入是：

| FSR 输入 | Veyra 现有资源 | 备注 |
| --- | --- | --- |
| color（渲染分辨率） | `srcRgba_` | FP16 线性，比 FSR 要求的 R8/FP16 更好 |
| depth（渲染分辨率，可选） | 无源分辨率深度 | 现阶段传常量深度；SR 不依赖它做重建，只影响边缘处理质量，必须如实标注 |
| motionVectors（渲染分辨率，像素） | 源分辨率光流（`flowTex_` 在当前图上就是源分辨率像素运动） | 需要按 FSR 约定确认符号（见第 3 节） |
| output（展示分辨率） | `workRgba_` | 直接作为输出，省掉回填 pass |
| jitterOffset | 0（内容本身没有 jitter） | 不造假 jitter；代价是细节累积不如游戏原生 |
| preExposure | 1.0 | 我们的颜色已经是线性工作域 |
| sharpness / enableSharpening | 0 / false（默认关） | 与现有 SR 行为一致，避免双重锐化 |

上下文生命周期：SR 上下文按最大渲染/展示尺寸创建一次（`maxRenderSize = 源分辨率`、
`maxUpscaleSize = 工作分辨率`），窗口/分辨率变化走既有 rebuild 路径重建；
`reset` 直接复用现有 reset 语义（open/seek/scene cut/设置切换）。

引擎/UI 侧要做的最小改动：`videoSrQuality` 现在 0–4 全部是 NVIDIA 后端，需要新增
"FSR 超分"选项（N 卡走 3.1.5，AMD 卡走 4.1 需在 AMD 机器上实测后放开），
并放开 `gd.enableSr = plan.srApplied && nvidiaAdapter` 这个非 NVIDIA 硬门控
（只对 FSR 后端放开，DLSS/RTX 视频超分继续只给 NVIDIA）。

## 3. 还没验证、不能跳过的点

1. **符号约定**：FSR FG 的重投影是 `previous = current + mv`（已在接入记录里核对）；
   FSR 超分必须按同样方法核对 FS R 3.1 upscaler PTX/HLSL，再决定是否传负号。
   不做这一步就接进去，结果是"能跑但拖影/抖动"，而且很难归因。
2. **深度缺失的代价**：当前只传常量深度，边缘/遮挡处理会明显弱于游戏原生 FSR 路径；
   要不要用现有的深度估计（Depth Anything 类）补上，是后续独立决定，不能顺手假装。
3. **AMD 卡 4.1**：本机枚举不到 4.x 提供方，任何"4.1 AI 超分已支持"的说法都必须等 AMD 实机。
4. **画质对照**：接入后要用同一段素材与 DLSS SR / RTX 视频超分做 A/B（现有
   `veyra_quality_probe` 的指标与人工看片），否则不能宣称"效果更好"。

## 4. 归属

与帧生成同一套运行库（AMD FidelityFX SDK 2.3.0，MIT），本地目录与 gitignore 规则见
[AMD FSR 帧生成接入记录](FSR_FRAMEGEN_INTEGRATION_2026-09-16.md) 第 7 节。

# 当前项目状态 / Current Status

更新：2026-09-18。发布状态与开发状态分别记录，历史计划不代表当前验收。

## 已发布 1.4.1

- RTX 30/40 DLSS 开启、6X 解锁及 3060 初始化/执行卡死修复；用户已反馈测试可用，不代表所有型号和驱动均覆盖。
- 修复持续播放后的补帧受限、源帧与生成帧调度、采集瞬时停顿；软件 FPS 是提交读数，不是屏幕物理刷新率。
- MKV 字幕与音轨选择、打开/拖动、设置保存、全屏播放阻止休眠等修复已发布。
- 导出保留实际编码/封装错误处理，已取消资格门禁与结束逐帧扫描。
- 原生 HDR 输入保留、HDR 基底增强和 HEVC Main10 导出已存在；这不等于 SDR 转 RTX Video HDR。
- Dolby/DTS 采集可解码为 PCM；不等于压缩码流直通或 Atmos 对象渲染。圆刚专项探索已收尾；独立诊断工具留在存档分支。
- Smooth Motion 由 NVIDIA App 管理，用户确认本机可用。Veyra 不修改驱动配置，不把驱动生成帧计入导出。

发布证据见 [1.4.1 执行记录](RELEASE_1.4.1_EXECUTION.md)、[更新说明](RELEASE_NOTES_1.4.1.md)。

## 本轮开发

帧同步已在隔离分支 `codex/frame-pacing-20260918` 实现并完成本机短测，基线存档 `6e69eeb`。入口：专业模式 → 运动 → 帧同步，默认关闭；低排队、均匀呈现、Reflex 实验和独立显示同步偏好可保存。无补帧文件测试减少了一帧提前缓存驻留；6X 提交吞吐保持 144fps，未证明显著降延迟或更平滑。Reflex 无补帧时真实调用 NVAPI，开启补帧时明确回退低排队并保留倍率；XeSS 继续由提供方调度。VC-007PRO 4K30 NV12、NR+DLSS4X 已完成四模式各 120 秒实卡测试，详见[采集阶段计时](CAPTURE_LATENCY_DLSS4X_2026-09-18.md)。屏幕端到端延迟、30/40 实卡、PS5、VRR 未验证，完整原计划尚未全部验收。其他验收见[帧同步报告](FRAME_PACING_ACCEPTANCE_2026-09-18.md)。未合并 main、推送、发布或更新现有 1.4.2beta 包。

采集呈现相位优化及原始 1.4.2beta 引擎同参数对照见[延迟优化与历史对比](CAPTURE_LATENCY_REDUCTION_2026-09-18.md)。保持 4K30 NV12、NR、DLSS4X，不使用换格式或降低倍率解释收益。

DLSS / XeSS 双侧长帧排查见[分段诊断与实卡证据](FRAME_STALL_INVESTIGATION_2026-09-18.md)。本机已重现 XeSS 窗口重建阻塞；粉丝正常播放的约 1.4 秒呈现阻塞、历史 DLSS 尖峰尚未归因。本轮添加分段和逐长帧日志，不宣称性能修复，不更新既有 beta 包。

RTX Video HDR 独立改动保留。用户已明确终止 NVIDIA FSR 4.1 实验并要求回退，不再执行此前 FSR4 施工和画质修复计划。

**最新决定：FSR4 画质不接受，已撤掉实验入口和接入。** 原有 DLSS SR、RTX Video SR、官方 AMD FSR 保留；旧实验预设 mode 6 读取时转为 RTX Video SR 高档，保留其他参数。HDR 在 SDR 显示器预览不执行转换，保留开关旁的真实状态提示。回退构建与检查结果见 WORKLOG。

- 存档提交 `0e3d4ac`，标签 `checkpoint/pre-video-hdr-fsr41-20260918`。
- 隔离分支 `codex/video-hdr-20260918`；工作区 `E:/项目/Veyra/worktrees/video-hdr-20260918`。
- RTX Video HDR 已接入共享图、设置和 Main10 导出；RTX 5070 / 616.56 上 Create/Evaluate、2X/4X/6X、NR+SR+6X、原生 HDR 回归、HDR 导出与缺库 SDR 回退通过。显示器 HDR 当前未开启，实际 HDR 显示、采集卡、PS5 与 RTX 30/40 未执行。
- HDR 里程碑提交 `3477ed2`；后续隔离分支 `codex/fsr41-nvidia-20260918`，同名外部工作区。
- NVIDIA FSR 4.1.1 INT8 的 UI、环境变量接入、后端适配、专用测试及构建/打包脚本已回退到 HDR 里程碑前的 FSR 实现。旧日志和外部研究产物只作失败实验记录，不作为当前可用功能。
- HDR 参数页在 1280x800 窗口已目视检查，滑块数值即时更新。全部测试、限制及本地包说明见 [本地测试记录](LOCAL_HDR_FSR41_TEST_2026-09-18.md)。
- 用户授权本地 `1.4.2beta` 内测群包，新增已核验的官方 TrueHDR 原件；未授权 GitHub push/Release 或 Agent 代发。便携包路径、哈希及解压后实测见 WORKLOG。实际 HDR 屏幕和 RTX30/40 HDR 仍待内测。

所有新产物放 `E:/项目/Veyra/`，源码/文档留在隔离工作区；逐项结果以 [WORKLOG](WORKLOG.md) 为准。

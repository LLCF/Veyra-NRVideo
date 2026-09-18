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

用户授权先更新文档，再实施 RTX Video HDR 和 NVIDIA FSR 4.1.1 实验接入。按 [施工方案](VIDEO_HDR_FSR41_EXECUTION_PLAN_2026-09-18.md) 推进。

- 存档提交 `0e3d4ac`，标签 `checkpoint/pre-video-hdr-fsr41-20260918`。
- 隔离分支 `codex/video-hdr-20260918`；工作区 `E:/项目/Veyra/worktrees/video-hdr-20260918`。
- RTX Video HDR 已接入共享图、设置和 Main10 导出；RTX 5070 / 616.56 上 Create/Evaluate、2X/4X/6X、NR+SR+6X、原生 HDR 回归、HDR 导出与缺库 SDR 回退通过。显示器 HDR 当前未开启，实际 HDR 显示、采集卡、PS5 与 RTX 30/40 未执行。
- NVIDIA FSR 4.1.1 固定提交的 INT8 研究 provider 已构建，五个导出符号与 CPU 查询通过；GPU 接入尚未执行。上游三槽资源复用及私有队列 reset 需要适配，不默认开放 UI。
- 本轮无 push、Release 或新增运行库发布授权。

所有新产物放 `E:/项目/Veyra/`，源码/文档留在隔离工作区；逐项结果以 [WORKLOG](WORKLOG.md) 为准。

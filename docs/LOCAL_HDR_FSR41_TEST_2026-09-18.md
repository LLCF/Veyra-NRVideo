# RTX Video HDR / NVIDIA FSR4 本地测试

这是基于 1.4.1 的本地实验构建，未发布、未合并 main。

## 使用

- 视频 HDR：专业模式 -> 增强页，向下滚动到 RTX Video HDR。默认关闭，支持对比度、饱和度、中间灰、峰值亮度；设置会保存。
- 预览需要 Windows HDR 与 HDR 显示器；当前显示器未启用 HDR 时保留 SDR 预览。离屏 HDR 视频导出不依赖显示器 HDR，使用 HEVC Main10 / BT.2020 / PQ。
- 原生 HDR 输入不再做 SDR 转 HDR。字幕与 OSD 在 FG 之后合成。游戏版 RTX HDR 没有加入。
- FSR4 provider 不随此包分发。研究构建在本机 `E:/项目/Veyra/build/fsr41-provider-20260918/Release/amd_fidelityfx_upscaler_dx12.dll`。仅对测试进程设置 `VEYRA_FSR41_PROVIDER` 为此绝对路径，并选择既有 FSR 超分档；关闭环境变量即走原来的官方 FSR。详情见 `scripts/fsr41/README.md`（包内 `docs/FSR41_EXPERIMENT.md`）。
- FSR4 失败会报告并关闭 SR，不以 FSR3 成功冒充 FSR4。原有运行库替换自由不变，身份日志不是加载锁。

## 实测

本机 RTX 5070，驱动 616.56 / 32.0.16.1656。以下是本机证据，不替代 30/40 系实测。

HDR：TrueHDR Create/Evaluate/Release 均为 0x1，SEH=0；独立 HDR、DLSS 2X/4X/6X、NR+DLSS SR+6X、原生 HDR 回归通过。90 帧 HDR 导出经开发期 ffprobe 与解码确认 Main10、10bit、BT.2020/PQ；缺 DLL 时 SDR 导出回退通过。未恢复产品导出结束扫描。预设 roundtrip 与 66 个旧格式迁移通过。参数页在 1280x800 窗口无文字重叠，滑块中灰 44 -> 59 即时更新；当前 SDR 显示不能证明实际屏幕 HDR 效果。

FSR4：CPU smoke 和六槽调用方 ABI 通过。使用上游固定提交的真实 ML passes，包含 R11G11B10 输入/输出适配。实验 DLL 874496 字节，未签名，SHA256 `F6EB66096E80CB9A8C066FA7519CF5C82442D0876C8C7A461F489402A6AD44F2`。

| 测试 | 结果 |
|---|---|
| 720p -> 1080p 持续处理 | 12000 帧，11675 次 SR，53.24 秒；command GPU 平均 1.84 / P95 4.34 ms；显存 345 -> 388 MiB |
| 1080p -> 2160p 持续处理 | 3600 帧，3502 次 SR，23.39 秒；command GPU 平均 1.66 / P95 3.88 ms；显存 1088 -> 1131 MiB |
| 1279x719 -> 1919x1079 | 90 帧 / 87 次 SR，D3D12 debug errors=0 |
| NR + FSR4 + HDR + DLSS 6X | NR 12、SR 10、生成批次 10，像素与参数变化检查通过 |
| 实际影片 720p -> 1080p | 同一 60 帧：普通缩放 SR=0、FSR3/FSR4 各 58 次；输出均非空并保持内容 |

持续处理是两个分别重启的进程，不能称为连续十分钟。GPU 时间为整条 command list，不是完整播放链路或单独 SR 的时间，不等于屏幕 FPS。首帧及每 37 帧 reset 时没有光流则暂不 SR，计数不伪造；pending reset 保留到下一次实际 dispatch。

实际影片同帧 PNG 位于 `E:/项目/Veyra/tests/fsr41-nvidia-20260918/media-{baseline,fsr3,fsr4}/fsr-frame.png`。已目视检查内容和几何，FSR4 与基线差异较小，合成棋盘存在边缘/调性差异；没有足够证据宣称画质提升。该对照不是长片动态拖影/闪烁验收。

## 限制与下一步

FSR4 上游模型的中间工作类固定 960x540，PRE/POST 支持不同几何不等于原生 4K 模型质量。视频没有游戏引擎的 jitter、真实 motion/depth；使用零 jitter、估计光流、常量深度和固定曝光。第一次 Create 实测 23.7 秒，后续进程约 0.5 秒；冷启动仍有明显等待。

未执行：RTX 30/40 新功能矩阵、真实 HDR 显示/跨屏、物理采集/PS5 HDR 转换、所有颜色格式和完整动态画质场景。现有 30/40 DLSS 兼容代码没有在本轮修改，但不以 5070 测试代替回归实卡。

下一步：HDR 显示器和 RTX 30/40 本地测试包验收；FSR4 在形成可靠的动态画质证据前保持实验入口。

## 产物与源码

- HDR 提交 `3477ed2`，分支 `codex/video-hdr-20260918`。
- FSR 分支 `codex/fsr41-nvidia-20260918`，源码在 `E:/项目/Veyra/worktrees/fsr41-nvidia-20260918`。
- 构建、测试、日志、临时文件分别位于 `E:/项目/Veyra/{build,tests,logs,tmp}/{video-hdr-20260918,fsr41-nvidia-20260918}`。
- 本地包目录：`E:/项目/Veyra/test-packages/hdr-fsr41-20260918`。二进制、SDK、影片与生成模型数据均未进入源码 Git。

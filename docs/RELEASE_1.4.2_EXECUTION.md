# 1.4.2 发布执行记录

## 范围与授权

2026-09-19 用户要求修复全屏提示、重新打包并发布 GitHub 1.4.2，更新报告重点为 RTX Video HDR 与帧同步。整合 codex/screen-capture-20260919 当前修改及其祖先隔离功能，NVIDIA FSR4 已撤回。远端用户 README 更新 5034212 一并保留。

新群二维码、HDR 对比图由本次用户提供，分别进入 docs/images/1.4.2/community-group.png 和 rtx-video-hdr-comparison.jpg。赞助原图未改，两图各 width=220。新群截图自身标明二维码有效期至9月26日，之后需用户提供新码。

TrueHDR 原件身份及许可证见 RUNTIME_COMPONENTS_1.4.2.md，逐文件打包校验；其余组件沿用，不含 SDK、模型、凭据、日志、PDB 或测试媒体。用户图片为明确授权的文档资产。

## 修复与验证

- AppShell.cpp：全屏切换 TTM_POP 清除已弹出的提示，TTM_ACTIVATE 在全屏期间禁用，退出恢复。
- delivery.ps1：显式 OutputDirectory/FixtureRoot，旧质量探针使用相对输出路径，避免 narrow path 损坏中文目录；不改变测试通过条件。
- 构建：scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/screen-capture-20260919 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/release-1.4.2-20260919 -DisplayVersion 1.4.2 -Targets veyra,veyra_quality_probe,veyra_capture_compressed_tests。exit0。
- 真实 GUI：打开 visible-scene.mkv，专业模式进入全屏检查。后续退出/再次悬停与正式包回归结果待补充。
- 首次 delivery 失败为质量探针依赖工作目录下 runtime_local/config；第二次图像保存失败为旧探针 narrow Unicode 路径。已隔离运行配置并用相对输出路径修正，保留失败日志，不作为产品通过证据。

此前本轮 UI、设备 FPS、WGC/DXGI、NR/DLSS4X 与便携包七组通过证据见 UI_CAPTURE_RATE_1.4.2_2026-09-19.md。30/40 HDR、反馈者5060、VC-007PRO设备限帧与PS5本轮未执行。

## 产物与当前状态

构建目录 E:/项目/Veyra/build/screen-capture-20260919。
本次 logs/tests/tmp 位于 E:/项目/Veyra/ 对应 release-1.4.2-20260919 子目录。
正式包计划位于 E:/项目/Veyra/releases/1.4.2，包含便携包、对应源码与 SHA256。
当前尚在本地验证，未发布；发布后补充远端 tag、资产、哈希与图片访问核对结果。

# 1.4.3 正式发布执行记录

2026-09-20 用户确认最新测试包可用，授权整理正确修复、合并主线并发布 GitHub 正式版，同时保留其中文 README 精简、同步英文只展示最新更新。

## 源码与分支审计

- 开工存档 `checkpoint/pre-release-1.4.3-20260920` / `139db25`。
- 修复分支 `codex/fg-cadence-audit-20260920`；合并远端中文 README 提交 `73709e0`，合并点 `c7c8219`。不恢复用户删掉的旧更新堆叠。
- 所有当前产品修复分支均在该分支祖先中。保留字幕弹窗 `4bce05e`、单项还原 `412a19f`、XeSS/跨屏/原画对比 `7761f34`、采集/音频/UI/同步 `5a1931b`、调度 `80f7dc4`、七项解码等修复 `43b8a4a`、后端切换 `8c9957b`、DLSS 恢复 `b2c3d0e`、后续保留的有界追赶/完成 fence/预算修正及用户倍率 `7a3dfae`、窗口栈修复 `2b99e89`、输入草稿/DPI `139db25`。
- `bb91399` 与 `88915a3` 对应实验分别被 `dc87a59` 与 `55e0bdd` 回退，维持回退。其余未证明收益的优先级/光流/复制/清屏/预测实验按实验文档保留历史，不恢复代码。

仅四个本地分支不属于 HEAD 祖先，全部不应合并：

| 分支 | 独有内容 | 处理 |
| --- | --- | --- |
| codex/avermedia-51-switch-20260917 | 8819ca8 未完成的设备音频探针 | 用户已结束该研究，保留存档 |
| codex/beta-latency-ab-20260918 | 24bc11a 旧 beta 延迟对照探针 | 测试基线，不是产品修复 |
| codex/github-source-archive | 2d2a3a6 / 8556fc7 历史源码归档 | 非当前产品分支 |
| codex/smooth-motion-experiment | 27c17eb 旧驱动实验和强制互斥 | 被用户自主叠加决定替代 |

以上按 `git for-each-ref --no-merged HEAD` 和提交历史核对，不以分支名称推断合入状态。修复详情见 RELEASE_NOTES_1.4.3.md 及其对应验收文档；GC551/GC573、固定 6X 均匀呈现和未定因清晰度反馈不冒充修复完成。

## 构建、包与来源

- 使用 `scripts/build-isolated.ps1`，BuildDirectory `E:/项目/Veyra/build/slider-reset-20260919`，DisplayVersion `1.4.3`；临时目录 `E:/项目/Veyra/tmp/release-1.4.3-20260920`。
- `scripts/package-portable.ps1 -Version 1.4.3` 不加测试标签，输出 `E:/项目/Veyra/releases/1.4.3`。保留原有 12 个增强运行文件身份与许可、PS5 patched FFmpeg，不加入新运行组件。
- `scripts/package-release-source.py` 从干净提交 `git archive HEAD` 生成对应源码，拒绝二进制/SDK目录。合并经 SHA256 核验的原 1.4.1 FFmpeg 和 RemotePlay 对应源码 ZIP；这些依赖与 1.4.2 未变，文件名保留原版本供追溯。
- 验收输出 `E:/项目/Veyra/tests/release-1.4.3-20260920`；已有针对性产品验收在窗口修复、补帧切换、采集/字幕和调度报告中。此次核对正式 EXE 与用户已验收 EXE 一致，并对正式包运行七项便携检查和逐文件归档校验。
- 只推送 nrvideo/main 与 v1.4.3；发布正文保留赞助和交流群两图各 width=220。源码标签不随后续发布证据记录而移动。

正式发布的验证结果追加于下方，以上步骤说明不是成功声明。

## 本地正式包验收

- 重新配置和构建 exit 0，Ninja 确认 no work to do；没有额外产品修改。EXE SHA256 与用户验收版相同：`F2A1E407D2FE136C32771BCF25068A6F595D51CC6B4F15766964B6A358B80676`。
- 完成最后 README 用法校对后，以 `releases/1.4.3/final/` 为最终输出目录。父目录首轮文档候选不发布；清理该候选的自动审批被拒绝，保留原件避免强行绕过，后续可按用户清理指令整理。
- `package-portable.ps1` 正式打包通过：12 个增强运行文件身份核验，禁止文件为 0。最终便携 ZIP 472351857 bytes，SHA256 `2CC1558B62849D7B71FDEA04CF6B010EF7825FD75F101AE456516C679A9EA0A7`。
- `portable-smoke.ps1 -CaseSeconds 7` 七项通过：空窗口、无效果播放、社区 NR + SR + FG、原版 NR + DLSS SR + FG、Video SR + NR + FG、首次效果全关、Ampere NR。收窄 PATH 并临时移走 manifest 后仍从包内加载依赖并生成截图；单项最长 35 秒。结果 `tests/release-1.4.3-20260920/portable-smoke/result.json`。
- 临时审计脚本 `tmp/release-1.4.3-20260920/audit-package.py` 对 ZIP 120 个载荷逐个校验大小/SHA256、数量和 README；所有 21 个 DLL 与 1.4.2 逐文件相同，EXE 匹配验收版。结果 `tests/release-1.4.3-20260920/package-integrity.json`。以上相对路径均位于 `E:/项目/Veyra/`。
- 依赖源码 ZIP SHA256 复核通过；`git diff v1.4.2 HEAD -- scripts/remoteplay scripts/ffmpeg licenses/remoteplay` 无变化。本次未重新跑完整 GPU/实卡矩阵，复用相同 EXE 的既有专项验收，新增正式打包回归。

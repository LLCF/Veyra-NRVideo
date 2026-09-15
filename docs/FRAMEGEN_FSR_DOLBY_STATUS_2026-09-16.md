# 帧生成 / FSR / 杜比：隔离分支施工状态（2026-09-16 凌晨）

给明早人工验收用。**全部工作只在隔离分支，未合并 main。**

- 存档点（main）：`693db07`，tag `checkpoint/pre-framegen-fsr-dolby-2026-09-16`
- 施工分支：`codex/framegen-fsr-dolby-20260916`
- 分支提交：`35dd632`（DLSS 6X）→ `4124a9d`（XeSS MFG 解锁）→ `e68b79d`（杜比探测 + 40 系实验开关）
- 计划：`docs/FRAMEGEN_FSR_DOLBY_PLAN_2026-09-16.md`

## 一、已完成并实测（可直接验收）

### 1. DLSS 6X（50 系原生，B 工作流）

- 改动：生成帧池 3→5/parity（`kGeneratedPoolSlots=10`）、`FrameBatch` 4→6、`interpolate` 上限 6、present SRV 堆 8→12、DLSSG 后端 `multiFrameCount` 上限 3→5、`validate()` 1..6；新增**能力驱动倍率**：UI 按运行时的 `MultiFrameCountMax` 只给 关闭/2X/3X/4X/6X，超上限的请求被 `requestSettings` 明确拒绝并保留原值，后端失败时**按上限降级并提示**（不再把补帧整档关掉）。
- 实测（RTX 5070 / 616.56 / DLSSG 310.7.0.0）：
  - `--fg-multiplier 6`：`FG capability available=true multiFrameMax=5`、`fg=1`、**445 真帧 / 2215 生成帧**（每帧 5 张）、exit 0。
  - 模拟 40 系上限（`VEYRA_TEST_FG_MULTIFRAME_MAX=1`）：`requested multiplier 4 exceeds GPU capability maxGeneratedFrames=1; applying 2X with frame generation kept`，312 真帧 / 310 生成帧，exit 0。
- 测试：`veyra_ui_contract_tests` PASS；`veyra_repair_contract_tests` 146 项 0 失败（新增 5X/6X 合法、7X 拒绝）；`veyra_repair_preset_tests` 48 组 PASS；`scripts/gates/delivery.ps1` 23/23 PASS，48.9 秒（`logs/delivery/89286afbb28d4785925ae3772e0409c4`），EXE SHA256 `9B8EA46AECF7A95E99472B7A31D60F9B5B9D8008D7B6BE0966C261095FDD0263`。

### 2. XeSS MFG 解锁（A 工作流，2X/3X/4X）

- 改动：新增 `include/veyra/gfx/XessMfgUnlock.h` + `src/gfx/XessMfgUnlock.cpp`，移植 OptiScaler（GPL-3.0，`70676c5f`）的**五处字节补丁**（U1–U5）：只改进程内映射、不改磁盘；补丁前校验模块身份（大小 + SHA-256 + PE TimeDateStamp/SizeOfImage）与每一处原始字节；全部位置先验证再写入，写入后回读校验，失败即回滚；XeFG/XeLL 上下文销毁后**恢复原字节**。`XessPresenter` 现在加载 `xefgSwapChainGetProperties` / `xefgSwapChainSetNumInterpolatedFrames`，查询真实上限、设置 `init.maxInterpolatedFrames` 并调用 setter；UI 只在"审计版运行库存在或会话已上报 >2X"时给出 XeSS 3X/4X。
- DLL 审计（本机随包 `runtime_local/intel/experimental/libxess_fg.dll`）：1.3.1.78 / 22,957,432 字节 / `EC5E0C65…AB27`；TimeDateStamp `0x69CB0F4D`、SizeOfImage `0x015ED000`；**U1–U5 五处原始字节全部匹配**。
- 实测（RTX 5070）：`--fg-xess --fg-multiplier 4` → `maxInterpolatedFrames=3 unlockApplied=true => maxMultiplier=4`、`framesPresented=4`（每真实帧 4 帧）、**563 真帧 / 1677 生成帧**、exit 0，退出时 `unlock rolled back 5/5 patch(es)`。
- 归属：`THIRD_PARTY_NOTICES.md` 已按 GPLv3 义务登记 OptiScaler（含固定提交）与 Magpie fork 的结构参考。

### 3. 杜比位流能力探测（G-1）

- `CaptureCardSource` 现在会分类压缩音频 subtype（AC-3 / E-AC-3(DD+) / DD+ Atmos / TrueHD-MLP / DTS 系列）并输出每设备汇总行。
- 实测（参考采集卡 USB3 Digital Audio）：`device bitstream types=0 [none] pcmTypes=15` —— **这张卡在硬件层就不提供任何杜比/DTS 位流**，因此"支持 Atmos"在当前设备上不可达；位流直通工作必须先换支持位流的采集设备（或改走 HDMI 直通链路）。

### 4. 40 系 MFG 前置实验开关（C-1）

- `VEYRA_TEST_FG_FORCE_MULTIPLIER=1`（仅测试用，UI 不可达）：绕过"请求倍率超过上报上限"的拒绝，让 40 系机器可以直接请求 3X/4X，观察运行库是**真的生成帧**、**重复帧**还是**黑屏/失败**。
- 用法（在 4060 反馈机上）：

```powershell
$env:VEYRA_TEST_FG_FORCE_MULTIPLIER='1'
.\veyra.exe --fg-multiplier 4 --smoke-seconds 15 <一段本地视频>
# 看日志：requested MFG multiplier unsupported / test-only FG multiplier forced above capability
# 以及 [app] smoke frames=... generated=... 与画面是否真的更顺（重复帧=数字翻倍但运动不增加）
```

## 二、未完成（明早需要决策或继续施工）

| 项 | 状态 | 说明 / 下一步 |
| --- | --- | --- |
| XeSS 节奏 hook（A-2） | **未移植** | 上游 `XeFGPacing.h` 需要重定向运行库内部 Present/Scheduler/Deadline 三处入口；我们目前没有 detour 基础设施，且 >2X 的生成帧间距是否成串**未测量**。这是 XeSS 4X 画质/节奏的关键未验证项，不能当作已完成 |
| 40 系 DLSS MFG 解锁（C-2） | **未实现** | 需要移植 `MFGAdaUnlock-RenoDx`（MIT）的两处架构比较补丁 + **内核 PTX 中点修正** + 强制软件 flip metering；PTX 修正是难点。在完成前不会把 Ada 的 3X/4X 伪装成可用 |
| 30 系原生 2X（D） | **未开始** | 需要 `dlssg_for_sm86` 代理方案与另一份 DLSSG 运行库身份，属发布范围变更 |
| FSR 帧生成（E） | **未开始** | 需先落 AMD FSR SDK 2.3.0（`AMD FSR Frame Generation 4.0.1` ML）并登记身份；本地 1.1.4 的 3.1.x 仅作回退 |
| FSR 超分（F） | **未开始** | AMD 卡 4.1 / N 卡 2、3.1 的分档 UI 与后端接入 |
| 杜比直通 / 解码（G-2） | **未开始** | 依赖支持位流的采集设备；当前设备已证实不提供 |

## 三、明早验收清单（建议顺序）

1. `git log --oneline -4` 确认三个提交都在 `codex/framegen-fsr-dolby-20260916`，`main` 未动。
2. DLSS 6X：`out\build\audio-continuity-repair-20260915\veyra.exe --fg-multiplier 6 --smoke-seconds 10 loop\local\fixed_clips\test_av_1080p.mp4`，看日志 `maxMultiplier=6` 与 `generated ≈ 5×frames`；UI 里补帧下拉应出现 `6X · 五张中间帧`。
3. XeSS 4X：`--fg-xess --fg-multiplier 4 --smoke-seconds 10 <视频>`，看 `unlock applied`、`framesPresented=4`、退出时 `rolled back 5/5`；**注意**：节奏 hook 未移植，观感是否均匀需要你人工判断。
4. 40 系实验：把 EXE 复制到 4060 机器，按上面 §一.4 的命令跑，把日志发我。
5. 杜比：用 `--smoke-seconds 10 <capture2:...>` 跑一次，确认 `capture-audio-bitstream` 行；若换到支持位流的设备，该行应列出 AC-3/DD+ 等类型。

## 四、边界声明

- 未合并 main；未推送远端；未发布。
- 磁盘上的 `libxess_fg.dll` 未修改（哈希与审计值一致）；所有运行库修改仅在进程内存，退出即恢复。
- 隔离分支上的 delivery 短测 23/23 通过，但**实卡 40 系、XeSS 节奏、FSR、杜比直通均未验证**，不作为已完成能力对外描述。

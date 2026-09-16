# 2026-09-11 继续修复目标模式执行中

## 2026-09-16 帧生成 / FSR / 杜比：隔离分支夜间施工（未合并 main）

用户要求"开工前创建 GIT 存档、建立隔离区分支、所有操作在隔离区进行、人工验收合格前不允许合并 main"。已建 tag `checkpoint/pre-framegen-fsr-dolby-2026-09-16`（main `693db07`）与分支 `codex/framegen-fsr-dolby-20260916`。

本轮完成并实测：

- **DLSS 6X**（提交 `35dd632`）：生成池 3→5/parity、`FrameBatch` 4→6、present SRV 堆 8→12、DLSSG 后端上限 3→5、倍率 1..6；新增能力驱动的倍率 UI 与"超上限拒绝/按上限降级并提示"。实测 RTX 5070：`--fg-multiplier 6` → `multiFrameMax=5`、445 真帧 / 2215 生成帧、exit 0；模拟 40 系上限（`VEYRA_TEST_FG_MULTIFRAME_MAX=1`）→ 4X 请求降级为 2X 且保留补帧。UI 合同 PASS、修复合同 146 项 0 失败、预设 48 组 PASS。
- **XeSS MFG 解锁**（提交 `4124a9d`）：移植 OptiScaler（GPL-3.0，`70676c5f`）五处补丁到 `XessMfgUnlock`；模块身份（大小 + SHA-256 + PE 标识）与逐字节校验、事务安装、回读校验、上下文销毁后回滚；`XessPresenter` 接入真实上限查询与 `SetNumInterpolatedFrames`。DLL 审计五处原始字节全匹配；实测 RTX 5070 `--fg-xess --fg-multiplier 4` → `maxInterpolatedFrames=3`、`framesPresented=4`、563 真帧 / 1677 生成帧、exit 0、`rolled back 5/5`；归属写入 `THIRD_PARTY_NOTICES.md`。
- **杜比位流探测 + 40 系实验开关**（提交 `e68b79d`）：采集音频 subtype 分类与每设备汇总；参考采集卡实测 `bitstream types=0 [none] pcmTypes=15`，证明该设备硬件层不提供杜比位流。`VEYRA_TEST_FG_FORCE_MULTIPLIER=1`（仅测试）供 4060 机器直接请求 3X/4X，观察真实插帧/重复帧/黑屏。
- delivery 短测 23/23 PASS、48.9 秒（`logs/delivery/89286afbb28d4785925ae3772e0409c4`），EXE SHA256 `9B8EA46AECF7A95E99472B7A31D60F9B5B9D8008D7B6BE0966C261095FDD0263`。

未完成（如实记录）：XeSS 节奏 hook（`XeFGPacing`）未移植、>2X 生成帧间距未测量；40 系 DLSS MFG 解锁（RenoDX 的架构比较 + PTX 中点修正 + flip metering）未实现；30 系原生 2X 未开始；AMD FSR（帧生成 4.0.x / 超分）未开始（SDK 2.3.0 未下载）；杜比直通/解码未实现。完整状态、验收清单与边界见 [隔离分支施工状态](FRAMEGEN_FSR_DOLBY_STATUS_2026-09-16.md)。未合并 main、未推送、未发布。

## 2026-09-15 采集卡直播窗口标题修复（第三方工具“识别不到 Veyra”）

用户反馈除 OBS 外各平台直播工具无法识别“正在采集中的 Veyra”，且顺序敏感：先抓到窗口再开采集卡正常，先开采集卡再抓就抓不到。实机取证确认根因是 Veyra 自己：采集卡来源 `capture:`/`capture2:` 连接串被当文件名写进主窗口标题，实测标题长 776 字符（`Veyra — capture2:<十六进制设备路径>:...`），空闲/播放文件时为正常短名；直播伴侣日志把该标题截断到 259 字符后参与来源命名，其包内前端以 `${exe} ${title}` 命名来源。同场会话的 mediasdk_server 日志显示“采集卡已运行再添加 game 来源”的 hook 通路实际成功（`Load Shared Texture Success, size: 842 x 494`、`OnAutoSwitchMode from Window to Game`、GameSource 连续 60 秒以上有数据），因此本轮不做换链/画面的猜测性改动。

修复新增 `apps/veyra/ui/SourceTitle.h`（`windowTitleForSource`），`AppShell::openFile` 对采集卡用 `Veyra — 采集卡 · LIVE`、PS5 用 `Veyra — PS5 Remote Play`、文件仍用文件名。构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0（4/4）；`veyra_ui_contract_tests.exe` PASS exit0（含新增标题合同）；新 EXE `--smoke-seconds 6 capture:9:0:-1:0` 实测标题 `Veyra — 采集卡 · LIVE`（18 字符），1.3.0 便携版同命令为 `Veyra — capture:9:0:-1:0`；随后用本机实体采集卡连接串（`capture2:`，1920x1080 YUY2@60）跑 `--smoke-seconds 12`：exit0、`frames=685`、`captureDropped=0`、`failed=false`，t+4s/t+9s 标题保持 `Veyra — 采集卡 · LIVE`。delivery 23/23 PASS、59.633 秒，`logs/delivery/7283c292af9e471bbba9c4bc8b0316af/result.json`，EXE SHA256 `0F1DEB29D80E80264CC5EE1D6D210D9415DEBC68EBA47DD22A1C73500D5700A4`，gate 自身仍标 `capture=awaiting_user_capture_test`。未驱动第三方 UI 复测（本机无可用 UI 自动化）、未 push/发布，候选 EXE 在 `out/build/audio-continuity-repair-20260915/`。完整证据、边界与下一步见 [采集卡直播窗口标题修复](CAPTURE_WINDOW_TITLE_FIX_2026-09-15.md)。

## 2026-09-15 VRR / 自动音频补偿延迟排查

用户反馈疑似采集卡在PS5开启VRR后自动补偿导致声音延迟、关闭补偿恢复。当前没有反馈者版本/实卡日志，不能认定VRR根因。新增生产CaptureAudioSession诊断`--sync-clock-audit`：正常可变观察间隔下补偿35.50ms、PCM队列13.75ms；保持合成画面延迟35ms而视频PTS后移1200ms时，补偿1235.51ms、PCM队列1214.56ms；同时间戳关闭补偿后0ms/10ms。真实WASAPI、合成PCM且增益0，不是实卡或声学测量。专项exit1保留失败，证明现有自动同步缺少时间戳可比性验证；未修改产品同步策略。

构建`cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0（2/2）；诊断用`scripts/acceptance/scheduler-short-test.ps1 -Name capture-sync-clock-audit-20260915 -Exe out/build/audio-continuity-repair-20260915/veyra_capture_audio_tests.exe -TestArgs '--sync-clock-audit'`运行11.60秒。完整失败证据、现有1.5秒目标/2秒队列边界、修改文件和修复方案见[VRR音频同步排查](CAPTURE_VRR_AUDIO_SYNC_AUDIT_2026-09-15.md)。未执行RTX Create/Evaluate、物理VRR或听测，未改DLL/SDK，未发布。下一步是基于同帧入口/呈现时间验证同步时钟关系，并取得反馈者日志确认真实触发机制。

## 2026-09-14 采集卡内部 FG 时间线修复

用户反馈 RTX 4070 Ti 开启内部 FG、NR/SR 关闭后很快显示过载。排查确认物理采集的源 PTS 与主机回调时钟存在 59.94/60Hz 长期漂移；旧代码只在会话开始锚定一次，导致补帧截止时间累积落后数百毫秒，软件准入连续拒绝 FG，并非已证实的 GPU OOM 或 NR/SR 负载。已在 `codex/capture-fg-clock-repair-20260914` 分支修复：物理采集/PS5/live replay 按每个 A/B 对重新锚定，保留对内 FG 节奏；新增 `timeline=capture-pair` 诊断，UI 将“过载”改为“补帧受限”。

修复前 checkpoint 为 `d9a94eb`。完整构建 216/216、最终增量构建 20/20；合同测试 109/109、实时计时、呈现 worker、DLSS 2/3/4 准入及 live overload/source-gap 回归通过。详细命令、日志和未完成的 RTX 4070 Ti 实卡验收边界见 [采集卡内部 FG 时间线修复](CAPTURE_FG_CLOCK_REPAIR_2026-09-14.md)。未 push/发布。

## 2026-09-14 采集卡音频设备选择修复

用户反馈“采集卡对应音频在 Veyra 中选不到、OBS 可以选择”。静态排查确认原实现只枚举全局独立 DirectShow 音频 filter，未使用选中视频 filter 的内置音频 pin，也用易变整数序号重新绑定设备。本轮已修复内置音频路径、独立音频 DevicePath 绑定、`capture2:` 连接串和音频 pin/media type 诊断；旧 `capture:` 路径兼容。详细范围、命令、结果和未测边界见 [采集卡音频设备选择修复](CAPTURE_AUDIO_DEVICE_SELECTION_REPAIR_2026-09-14.md)。

最终 `x64-release` 构建 exit 0；`veyra_capture_tests --list`、普通/5.1 音频 jitter、UI contract、capture color 均通过。本机只枚举到独立 `USB3 Digital Audio`，没有反馈者实卡，未宣称实卡验收；未 push/发布。

## 2026-09-14 用户实卡反馈：RTX 3060 XeSS-FG 2X 可用

用户确认在 RTX 3060 实机上，Veyra 选择 `Intel XeSS · 实验显示补帧 2X` 后可以正常使用帧生成。该结论指向 XeSS-FG 预览路径，不等同于 NVIDIA 官方 DLSS Frame Generation 对 RTX 30 的支持，也不证明社区 `dlssg_for_sm86` 已接入或必要。

本条证据来源为用户本轮实机反馈；本轮未由 Agent 重新执行 GPU 日志、外部捕获或长时稳定性测试，因此只记录为“用户确认可用”。当前仍限于实时预览 2X；视频导出、3X/4X、长期画质/鬼影、采集/OBS 捕获和端到端显示帧率未由本条验收。未修改代码、SDK 或 XeSS 运行文件。

下一步唯一任务：如需扩大支持声明，再收集 RTX 3060 的运行日志和长时/画质对照；在此之前不新增 RTX 30 特判或替换 XeSS 运行时。

## 最新交付：0.0.3 已发布

GitHub发布完成：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.3 。源码提交`83f6353803861307b7003213dc510a837b6f3966`与v0.0.3标签已推送，公开时间`2026-09-11T05:09:04Z`，非草稿且标为latest。四个附件大小和远端SHA256逐项匹配，README远端blob与提交一致；0.0.2资产/标签未修改。以下候选阶段记录由本条发布结果闭环，随后仅提交发布记录。

用户授权更新 `Likely7/Veyra-NRVideo` 源码和0.0.3便携包。中英文README已补OBS窗口采集手动选择Windows 10（1903及以上）的方法；用户实测恢复视频，OBS日志确认由BitBlt改WGC，未继续修改软件提示。0.0.3包含双NR运行版本、直播实验开关、v10预设和中文MSVC/Ninja依赖修复；专业UI大改仍仅为方案。

全新构建174目标通过；最终EXE `D9C7DCCEA7B1538066CC648D7C8E0D5513439A367E0999A70E614556FAB6395C`。CPU81项、预设42组、项目外解压清洁PATH/无manifest五条路线、两种NR实际包内路径、UI切换与回退、XeSS/直播开关、最终delivery23项47.474秒全部通过。NR/DLSSG Create0x1/SEH0，实际GPU执行及4K非黑保存通过。日志 `logs/release-003-portable`、`logs/delivery/cb1f9a5f77524603a6bd29d1d0f630e7`。

包298999496字节，SHA256 `3A37336BF09177A8224AA5F15C2FB9333AF5657F37F3B86411DD4AB0BC9E1D7B`，52文件逐项审计通过。社区DLL按用户本次发布决定列入Release manifest，标明HashMismatch，与原版独立；SDK/DLL/模型不进源码Git。完整命令与范围见 [0.0.3执行记录](RELEASE_0.0.3_EXECUTION.md)。RTX40、真实设备长期/音画/录制节奏未新增验收，远端发布结果另记。

## 最新交付：直播兼容实验开关与专业UI布局方案

按用户最新决定，撤回全局FLIP_SEQUENTIAL和OBS进程检测，默认FLIP_DISCARD；专业参数顶部“还原默认”右侧新增固定“直播兼容 · 实验”开关，勾选切FLIP_SEQUENTIAL。即时设置事务排空/重建，模式GetDesc1实查，总增强关闭仍可单独切换，v10预设保存。实际状态明确显示显示模式；不把开关生效标作第三方捕获成功。专业模式重排只给方案，尚未实施。

标准入口已构建，SHA256 `041A14751E182936B21A8878AE7C3508C96EF9009FA9C1D05171121315A82F74`。CPU81项、预设42组通过；实际RTX5070 NR/DLSSG Create0x1/SEH0，DLSS和XeSS两种UI往返、暂停/全屏/总增强关闭和两尺寸布局通过。delivery23项42.881秒通过，`logs/delivery/e6eb77b2bcc5468781d458f7e5371e77`。命令、失败和未测项见 [直播兼容执行记录](BROADCAST_COMPATIBILITY_PLAN_2026-09-11.md)，[专业UI布局方案](PROFESSIONAL_UI_LAYOUT_PLAN_2026-09-11.md)含六分类、文案和验收标准。

前序“FLIP_DISCARD子窗口就是捕获失败根因”缺乏直接捕获证据，本轮不继续作为事实传播。没有验证外部软件捕获、实卡、社区NR组合和超大图片专项，不宣称已降低占用或已解决捕获故障。下一步用户用同一捕获源切换对照；未push/发布，DLL/SDK仍隔离。

## 最新交付：原版与RTX40/50社区NR运行版本切换

入口补交付：用户截图未见选项，查到两个运行窗口均来自旧 `out/build/x64-release/veyra.exe`，初次仅提供隔离构建链接不够。旧进程随后已退出，未强杀；已清理标准构建目标并完整重建174目标exit0，根目录 `Veyra.cmd` 现在打开新版。标准EXE SHA256 `4AB989BC9223F8EE821CF449CE7A0F2DE14C49F7C3C08E1ECC4299DC411E88D4`，原位置再次实际UI切换/回退/两尺寸布局PASS，`logs/nr-runtime-switch/1789100030646506400/result.json`，构建日志 `logs/nr-runtime-default-entry-build.log`。此条取代下文“旧入口未替换”的初次交付状态，GitHub资产仍未更新。

专业模式增强页新增“NR 运行版本”，默认原版，可手动选择用户提供的RTX40/50社区实验版。两份DLL保留独立目录；设置事务重建和失败回退、预设v9、实际运行状态、播放/采集/分块图片/视频导出已接入。社区文件SHA256 `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`，签名HashMismatch，原样保存在忽略目录，不称有效签名原版。

本机RTX5070实际社区Init/CreateFeature18 `0x1`、SEH0，播放239次NR且输出非黑；实际UI往返和缺文件回退通过；社区SR+NR+FG导出4K/120fps含12源帧、11生成、1显式CFR补齐；独立worker社区NR导出120帧，前台继续运行。CPU81项、预设36组、delivery23项43.129秒通过。最终EXE SHA256 `6C3723B1E0F3E10BA456905E39F94824A7E650C5E10E28545201DFCE73A75976`，新版入口 `out/build/release-0.0.2-final/veyra.exe`，用户原进程和根目录旧启动入口未替换。

同时修复实际发现的MSVC/Ninja中文头依赖前缀错误：旧增量构建漏编译设置结构使用方导致启动ABI崩溃。改为实际编译探针获取原始前缀后完整重建174目标，最终ExportJobManager记录13个头依赖。完整命令、修改文件、失败记录、日志与边界见 [NR双运行时交付记录](NR_RUNTIME_SWITCH_PLAN_2026-09-11.md)。RTX40真实硬件、实体采集卡及社区超大图片专项未执行，下一步为RTX40实机验收。没有push或替换0.0.2 Release，没有向Git加入DLL/SDK。

## 最新交付：0.0.2 便携发布与用户DLL替换

已发布：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.2 。源码提交`a69a9df`及同名标签已推送，四个附件远端SHA256/大小逐项匹配；公开时间`2026-09-11T03:22:48Z`，`isDraft=false`。原`Veyra-DLSS-Video-Player`远端未更新。下文候选阶段的“另记实际结果”由本条闭环，随后仅提交发布记录。

用户授权发布到`Likely7/Veyra-NRVideo`，并要求去掉运行时校验。取消XeSS固定哈希/签名锁、移除诊断中的假定原件hash、支持Release运行目录；绝对路径/API初始化检查保留。发布者默认包仍做来源/身份审计，不向Git提交SDK/runtime。中英文README、教程、Release Notes与组件说明已重写；完整命令、文件范围、失败及验证边界见 [0.0.2执行记录](RELEASE_0.0.2_EXECUTION.md)。

新目录完整构建通过，修复首次配置FFmpeg依赖顺序与便携XeSS CRT查找。最终EXE `A07B73C2CD946AD50FD8516A51DB3B0CD6759945BB85E82D65F0D8CEB18B6170`；CPU81项，最终delivery23项42.929秒，解压后清洁PATH/无manifest的基础与SR+NR+FG两路线、XeSS/DLSS UI切换及非黑4K保存通过。真实Create为`0x1`/SEH0；本机VSR实际走驱动NGX实现，未证明包内VSR DLL被使用。日志`logs/release-002-portable-verified`、`logs/delivery/130f307d2be14370876e63b6a9f1c6f9`。未新增实卡或长期验收。

最终包`out/releases/0.0.2-final/Veyra-0.0.2-win64-portable.zip`为185295449字节，SHA256 `D1A6D37D61D1E61F8D700EC534DFA718F1909C6781F1AF47F1CFD4955AE4E2C3`；另有FFmpeg开源对应资料ZIP与校验文件。源码/历史审计未发现SDK、运行DLL或模型；本次仅上传授权的Release资产。远端提交与发布确认另记实际结果，不以候选构建代替发布。

## 最新任务：采集延迟对照排查

用户反馈采集预览慢于OBS/PotPlayer，重新开启这一范围的排查。基线`4e2bd73`、干净工作区；只发现OBS进程，未抢占设备。本机OBS日志为1080p60/YUY2/缓冲关闭；官方32.1.2源码确认非缓冲取最新帧。`v0.0.1`仍走强制RGB32旧链，不能把本机开发修复当成发布用户已收到。

确定缺陷：物理采集FG关闭仍按首回调锚定的源PTS等待，首帧迟到/时钟漂移能扣留已处理画面。本次改为GPU-ready驱动，保留文件/测试回放及FG节奏；新增策略日志。旧测试仅测产品未调用的`livePairHoldMs`，本次新增直接调度器复现：构造首帧晚12ms/后帧处理3ms会多等9ms，修正后消除该等待；不是实卡测量。

完整命令、修改文件、证据和未测项见 [采集延迟排查](CAPTURE_LATENCY_AUDIT_2026-09-11.md)。构建`logs/capture-latency-build-20260911.log`exit0，81项合同/41项时序/28项实际RTX回放PASS；delivery `1be4f528d272465585c96ada5b9b88e3`23项42.907秒PASS。Feature18 Create与DLSSG Create/Evaluate `0x1`/SEH0。应用SHA `CDCB2303402438FC208E00B6F9F88708B16249FEB32410FE6E5D484FBBB15747`，运行时身份有效，无SDK/runtime提交，无push或Release更新。

显示队列、MJPEG兼容解码和XeSS重复节拍等待仍是待测方向，不宣称是反馈的全部根因；实卡即时分支、同源外部延迟/扫描、音画验收未执行。下一条任务是确认反馈者版本与采集格式，全部增强关闭做三软件同源A/B。下方“性能暂缓”为上一任务范围，不覆盖本次新指令。

## 最新交付：FRUC 与诊断收尾完成，等待用户验收

按最新用户六项决定收尾：彻底删除FRUC后端/worker/协议/命令行及旧测试，v8预设显式迁移v4-v7的FRUC和XeSS，打包脚本修复，旧运行DLL/EXE四文件清理。修正之前把暂停恢复/PTS跳变/设置记成切镜或Resize的诊断错误；新增固定8192事件轨迹及覆盖统计，接入现有脱敏诊断预览，不逐帧写磁盘，不改画质/调度和导出检查。

完整命令、修改范围、失败及哈希见 [本次收尾记录](FRUC_REMOVAL_AND_DIAGNOSTICS_2026-09-11.md)。`scripts/build.ps1`最终exit0；合同77项、预设30组、实际RTX引擎28项、DLSS/XeSS UI正常和拒绝回退均通过；delivery `ef265e2874624e6aa6e80f7a8c34f8d8` 23项42.852秒PASS。真实Feature18/DLSSG Create/Evaluate `0x1`/SEH0；应用SHA `E078E0B9348841E8A109E1160E704862F175B43F7C5E07C1ADB1D50F73432804`。初次轨迹编译声明顺序错误与一次脚本数组传参失败保留，修正后最终回归通过。下方da5c1b4缺陷清单已由本次修复覆盖。

AMD NR搁置，性能/UI不继续扩展；实卡、真实设备音画同步/拔插和跨屏DPI未由Agent验收；不打包/push/发布。下一步是用户启动 `Veyra.cmd` 实测，不因旧待办继续扩大任务。

## 2026-09-11 用户搁置 AMD NR / 当前未完成核对

用户最新决定：AMD NR 路线暂时搁置，停止本轮上游调查，不作为当前修复完成门槛。没有实现 AMD NR，不将已有 AMD 光流写成 NR。下方历史的“下一条 AMD provider”不再适用。

当前源码 HEAD `da5c1b4`。FRUC 移除仅有部分代码及文档提交，不能称完整交付：`FrameGenerationBackend` 仍保留 `Fruc = Dlss` 别名与 `--fruc` 路径；UI 回归仍按 FRUC/XeSS/DLSS 三项索引；PresetStore 只迁移 v4/v5，旧 v6/v7 的 FRUC=1 会误解释为 XeSS（倍率过高则拒绝），旧 XeSS=2 被新枚举验证拒绝。schema 仍写 v7，需修正持久化兼容合同。打包脚本第26行删除 FRUC 后留下末尾逗号，PowerShell Parser 实测 `Missing expression after ','`。最新提交尚未完整构建/回归；前序 EXE 与 delivery 通过不能覆盖该提交。项目已有 `scripts/build.ps1` 负责定位 VS/CMake，前序终端 PATH 找不到 cmake 不构成工具链不可用的证明。

剩余当前工作：先完整收尾 FRUC 移除及旧预设迁移，更新关联测试并构建/针对性回归/必要 delivery；随后补齐 reset 原因结构化及有界逐帧性能轨迹。单GPU所有者、实际完成帧率/阶段计时、设置生命周期、文件音频恢复和可控淡出已有前序软件证据，但未证明原生4K NR+SR+高倍率FG的性能问题全部解决或达到对照软件水平。真实采集卡的组合吞吐/节奏/A-V、物理音频设备拔插、真实150%/200%系统跨屏DPI尚待验收。默认音频设备改变而旧端点仍有效时尚不主动迁移。发布包仍未更新，文档历史快照须由当前状态覆盖。

本轮实际只读检查：`git status --short`（开始干净）、`git log -6 --oneline`、读取当前方案/交接/实施记录、检查预设与后端代码、PowerShell Parser 解析打包脚本（上述失败）。未构建，未执行新的 RTX Create/Evaluate、AMD 或实卡测试，未 push/打包/发布。下一条唯一任务：FRUC 移除的兼容性、测试与构建收尾。

最新 reset/rebuild 计时：`FrameFlowMetrics` 增加带 session/revision/epoch/source identity 的设置生命周期记录，区分轻量重置与资源重建，并记录 drain/destroy/create/warmup/first GPU-ready 五段 CPU 观测、完成/回滚/取消/失败结果。真实设置事务、暂停缓存预览、注入失败回滚、停止中取消均已通过 `continuation-reset-final-rollback`（约8.8秒，32项，合成文件回放）。完整 release 构建 `reset-lifecycle-build-final.log` exit0。未执行实体采集卡、真实系统跨DPI或AMD provider；目标仍active。
普通 open/history boundary 随后接入同一记录，reset counters 改为累计；source-gap/EOF 最终 `continuation-reset-cause-live-final` 62.6秒通过（3600 real frames ready/presented，0 cancelled）。早期20/30秒窗口失败原因是素材在本机需要约一分钟读完，失败日志保留，测试上限调整为90秒（单次 watchdog仍290秒）。

最新可控淡出：基线`78675f2`，为采集设置/PTS/大偏差重锚和文件播放中seek加入真实240帧PCM淡出，WASAPI padding消耗后Reset再淡入；边界最长80ms，取消/错误退出，正常pump不加等待。55项timeline、7项真实player、11.105秒合成采集通过；重锚额外等待约16至40ms，普通偏差10.6至21.3ms。最终delivery `6304c4518f864f34a4bb91392cd16ab1`23项PASS，应用SHA `A86DB75E321677C2B9EDC0380D3BC4DCC13FAD3DE5BA05D59DA5C1C30C6BB97A`；命令、测试身份与边界见实施记录最新段。真实RTX执行，未实卡/外部听觉验收，无发布。下一任务补reset排空/销毁/创建/预热恢复计时及逐帧轨迹；AMD完整网络/provider仍缺，目标active。

最新文件音频恢复：基线`bba7283`。修复pump失败退出音频线程和clock失效后视频转墙钟继续播放；文件端点改由音频owner创建/500ms重连/释放，clock读取与COM生命周期互斥，最后有效PTS重锚、断开期间seek更新目标、暂停预填不Start，状态区显示恢复/HRESULT/次数。最终音频47项2.628秒、真实player7项2.297秒PASS；原生4K过载5项及合成采集回归通过。首轮任意并发读取次数门槛失败已保留并改成验证实际并发生命周期，详见实施记录最上方。最终build `audio-file-recovery-final-build.log`exit0，delivery `5c80d299087b4f2482706bf2a24df9c9`23项42.420秒PASS，应用SHA `E2EA9CB93CB290E6B13683647835B3E7F54EA21810FC358D69DB803AA9168866`。RTX NR/NVOF/NVENC已实跑，未实卡/AMD/真实系统设备拔插。下一任务可控音频重锚短淡出与原方案剩余计时覆盖审计；AMD依赖/provider、高DPI实机和实卡未完成。整体目标active，本地存档，不发布。

最新AMD诊断（UI已经提交`ee46c5f`）：隔离探针调试输出明确`Preview releases of D3D12Core require Developer Mode.`，随后CLSID_D3D12CoreModule获取失败；已解释先前0x887E0003，尚未到GPU能力查询。`continuation-amd-debug-loader`0.088秒exit1，完整stdout/result在`logs/scheduler-repair-20260910/`，只调试自有进程。未开开发者模式、未改主程序runtime、未提取权重。AMD完整网络/provider仍未实现。下一条独立任务转文件音频端点失败恢复，避免共享renderer shutdown与引擎clock读取竞态；详见实施记录最新段。整体目标active，不发布。

最新UI续接（优先于下方历史）：基线`db98af7`。修复最小高度状态区无明细空间、90像素滚动跳过选择器、DPI切换字体14变15、窄标题绘制区域重叠；统一应用DPI，并只给smoke进程提供96/144/192注入。最终`ui-layout-1789065295999940200`三档布局/字体、最小窗口滚动、popup、滑条滚轮不变值、展开中间帧、resize、最大化/全屏进出/自动隐藏、黑色视频区PASS；实机系统DPI96，150%/200%仅应用注入，真实跨屏未测。首轮因FG选择器被滚动步长越过失败，证据保留。绘制21项、popup14种、UI合同通过；实际RTX后端UI`ui-fg-1789065217190095800`完成FRUC/XeSS/DLSS/关闭，DLSSG Create/Evaluate0x1。构建`ui-dpi-build2.log`exit0，EXE SHA `27910DEDB2855334C45A0B92F4694542AB8631FB9AA17FED819E65585CC35C55`。详见实施记录最新段的命令/日志/限制。本轮未跑NR、实卡、AMD或新的联合delivery；未改导出检查。下一任务AMD NR依赖/provider，整体目标active，本地存档不发布。

最新单GPU所有者续接：以`e25585e`为基线，用`LiveGpuScheduler`取代呈现线程并删除旧worker；提交/完成检查/deadline/Present由引擎线程推进，容量仍2，实卡tryRead立即返回。空档仍完成已有帧，EOF先呈现尾批，退出边界不覆盖失败；时间戳在源Waiting时也采集。状态机15项、最终live30项、400ms输入空档/62帧EOF6项、FRUC24项及正常/拒绝XeSS界面通过。独立旧Git构建同源原生4K NR+VSR4+FRUC4对照，新旧均33源fps、P95约59.66ms、0有效生成，不声称性能加速。首次CMake依赖顺序/无NR缺shader目标一起修复并全新配置验证。最终delivery `3462b28549ed4982a99799adc5a3071e`23项42.380秒PASS，EXE `1B0DF186574B552E349EBB9502AF89A001430B89407B3D15635862E668279CE6`。细节、失败与完整命令见实施记录最新段。下一条全DPI/窗口交互验收，AMD NR/实卡仍未完成；本地存档，不push/发布。

最新GPU计时续接：音频已提交`1821120`。修复`GpuTimer::collect`多帧完成只保留latest及同源补帧查询槽冲突，启用64条有界完成记录、逐帧/epoch/revision统计，满槽与溢出显式记录。真实RTX时间戳6项、CPU71项、最终live30项通过，30fps时Color一秒样本30。最终delivery `88753f608465497dba0c2660bf7755dd`23项42.146秒PASS，应用SHA `F6B5F0E96B81B223FE856E888221CFF51ABBE706C9898A6609A49B7EF90413A2`。命令/身份/边界见实施记录最新段。下一任务完整单GPU所有者调度；仍未实卡/AMD NR/全DPI验收，不push/发布。

最新漂移续接（覆盖下面旧结论）：在`ed5aed3`之后接入有界swr补偿和重采样PCM到IAudioClock的分段PTS映射。候选关闭/零补偿出现53.8ms回归，已定位并修复采集填充静音及等待顺序；最终普通模式11.112秒、端点恢复3.573秒、文件/半速/断粮时钟1.082秒和70项CPU合同通过。同一最终EXE正负1000ppm各120秒，P95为4.242/4.302ms，均missing0/仅首次reset1。联合delivery `c41ad55982e6448190d1bc4ed239a415`23项42.484秒PASS，应用SHA `FDA98B226562C55B44A8139E1A4D7F3F35A7458109AC8ED4F72FD6ACE755D6BE`。详细失败、命令、哈希见实施记录“采集时钟漂移与真实PCM播放位置”。本轮建立本地存档，无实卡/发布；下一任务逐帧GPU时间戳交付，随后单GPU所有者，AMD NR与UI完整验收仍未完成。

最新音频续接（优先于下方）：统一raw+转换中+PCM总预算500ms，突发600块high-water500ms通过；WASAPI实际HRESULT诊断、500ms重连、自有端点释放恢复及首缓冲淡入接入，正常11.12秒/故障3.59秒/文件时钟0.523秒测试通过。联合delivery `0325b097b61a42b0becf6dc71a900175`23项42.43秒PASS，应用SHA `1136543C02604020F877BAA8BE0F67442671DFC7805FBB28310CD57B2D3E8FC6`。但新增120秒-1000ppm漂移测试exit1，P95偏差31.73ms、3次额外重锚；长期平滑同步未完成，失败完整保留。下一任务为漂移重采样及播放PTS映射，详见实施记录最新段。没有打开实卡或操作系统默认音频设备，未push/发布。

## 最新续接：设置隔离、生成帧关系与FRUC过载筛选

已把前序45个源码/文档文件提交为本地checkpoint `6316376`，未push。此后新增修复：纯音频设置不推进GPU revision、不重置视频历史；重复设置通知不提交；排空后读取最新配置，视频失败保留随后独立音频修改。A/B真实到达关系与生成呈现距A/B时间进入集中状态和有界一秒统计。FRUC按实际SDK调用轮转CUDA输入，修复跳过源帧后覆盖仍被SDK引用的前帧；恢复后像素2X/3X/4X通过，并重新启用实时采集提交前筛选。

同素材/同EXE/同worker原生4K NR+最高VSR+FRUC4X过载对照：基线150源帧、450次FG Evaluate、处理12fps；筛选后450次提交前跳过、处理33fps。两组有效生成呈现均0，基线未记录过期有效帧，不能说450张有效插帧全被丢掉，也不能当FRUC内核加速。源帧到Present返回P95约90.68→60.13ms，仅文件模拟采集，非HDMI/扫描延迟。正常known-pan15回放继续有效生成。

Release构建 `build-fruc-call-parity.log` exit0；合同66项PASS；真实RTX live含音频隔离/生成关系/30↔60/NR/FG2X4X通过；FRUC skip像素2/3/4、正常controller24项通过。最终delivery `logs/delivery/f024e12327b5429593e762943abb2eca/result.json` 23项PASS，详细命令/失败/哈希在实施记录本次续接段。未打开实卡，未跑AMD NR。源码checkpoint后继续目标，完整单GPU所有者、逐帧GPU计时覆盖、音频漂移/队列和AMD仍未完成。下方“未commit/待测”等为前序历史，不覆盖本条。

最新状态以 [实施记录的2026-09-11部分](CONTINUATION_REPAIR_IMPLEMENTATION_2026-09-10.md) 为准。已推进非阻塞完成观察（deadline/反压期间计数）、53项统计合同、真实RTX23项回放，以及文件过载/暂停seek和受控采集音频恢复。采集音频首版欠载重置过于激进的失败已保留，改为持续断流后重锚；11.05秒恢复测试通过，非实卡验收。

FRUC取得新实证：应用自有CUDA arrays+D3D11纹理桥接、graphics map/unmap、同CUDAcontext/批量互操作，替代旧FRUC内部DX11同步路径。六次重置/向后PTS/颜色反转的2X3X4X像素GT通过；reset现在只重种首帧，不常规重启worker或CPU排空队列。最新批量版本4X正确性通过，2X3X/集成复测待做。未加入cuCtxSynchronize或产品像素回读。原生4K4X吞吐测试仍慢（初版FG区间70.58ms），1080p批量版本约26.05ms，不能声称整体性能已经解决；SDK map/unmap自身可能阻塞。

实际命令均为 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release` 和 `scripts/acceptance/scheduler-short-test.ps1 -Name <unique> -Exe <test> -TestArgs <args>`；完整名称、SHA、失败/通过与路径见实施记录，单次<300秒。AMD NR缺依赖/目标硬件且provider仍未接入，整体目标active。未commit/push/发布；下面2026-09-10内容为前序进度，不覆盖本条。

## 2026-09-10 前序进度

续接已修文件PCM时间戳/首缓冲/seek/解码尾包，并通过真实WASAPI与44.1/48k样本数回归；新增采集受控PCM/WASAPI与自动/手动/关闭同步设置，合成80ms视频延迟回归通过（平均软件偏差12.8ms、0溢出/欠载），实体采集未测试。帧率窗口不再因Drop连续清空，51项合同通过。FRUC双向D3D11桥接候选失败已撤回；AMD固定源码取得，但网络权重/preview工具链及目标硬件未就绪，provider未完成。详情及失败日志见实施记录。文件过载共同缓冲刚写待测，目标仍执行中；未commit/push/发布。

用户明确开启目标模式，已建立目标并开始改代码。当前证据和未完成列表见 [继续修复实施记录](CONTINUATION_REPAIR_IMPLEMENTATION_2026-09-10.md)。R0补帧选择器布局及XeSS失败回滚已通过真实GUI正常/故障注入测试；R1统计与状态区首版已构建，49项合同与23项采集回放回归通过。AMD NR、完整FRUC/调度、音画同步仍待实施，未宣称全目标完成。单次测试<300秒，未测试实体采集卡，未发布。

# 2026-09-10 后端切换、真实产出与音画同步继续修复方案

用户要求先写新方案、汇报准备修复。本轮交付 [CONTINUATION_REPAIR_PLAN_2026-09-10.md](CONTINUATION_REPAIR_PLAN_2026-09-10.md)，包含XeSS可见切换、独立AMD/NVIDIA NR、真实处理产出FPS、集中链路/总延迟、采集和文件音画同步，以及上轮未完成的FRUC reset与S3/S4。没有修改产品代码或把方案当已实现。

本地基线 `be00e27`，进入本轮时Git干净。源码确认：SettingsWindow后端选择器208的y=8与标题1103的y=12重叠，尚未进行本轮UI复现；AMD当前只有光流、没有NR provider；`s.fps`统计GPU完成的真实源帧而非直接输入FPS，但不含有效生成；状态区只展示NR圆环和源帧速率。采集音频通过DirectShow自动renderer直通，未与GPU视频对齐；文件已有WASAPI音频主时钟，需保留并修复必要边界。

核对本地Intel XeSS3.0.2官方header：`framesPresented`是上次调用送往呈现的帧数，不是逐帧GPU完成或屏幕扫描计数。新方案要求明确标为SDK提交；主处理产出与呈现提交分开，不能乘倍率或用旧计数伪造高FPS。延迟按同帧首尾时间直接测量，各阶段不能重复相加；音频采用共同PTS和有界补偿，不承诺消除计算延迟。

同步HANDOFF与README当前入口，给既有研究/调度计划加入续接链接。旧Loop与Phase规则继续归档。只执行Git状态/日志、rg/Get-Content源码和文档读取、本地SDK接口核对、用户截图查看与文档编辑；本轮未构建、未执行新Create/Evaluate、AMD网络、FRUC测试或实卡，也未操作用户当前应用。未push、发布或打包运行时。

文档验证：`git diff --check` exit0；PowerShell链接/围栏/空白检查通过，覆盖7份文档、29个本地链接，新方案也检查了未跟踪文件的行尾空白。Git仅提示既有CRLF自动转换设置。一次多文件patch因旧标题不匹配整体拒绝，没有写入；读取实际标题后重新应用成功。这些结果只证明文档一致性。

下一条任务：R0，修专业页XeSS选择器的布局/命中并验证实际后端切换；完整任务顺序和真实阻塞处理见新方案。上轮的软件通过与FRUC失败证据保留，不冒充本轮新增修复结果。

# 2026-09-10 旧 Loop 退役与调度修复

用户明确废弃初期 Loop；AGENTS/README/旧 Playbook/ACTIVE_DELIVERY_PLAN/gates README 已标明旧控制哈希、STOP、Phase 队列不再阻塞当前修复，未改 CONTROL_HASHES 自我放行。delivery 删旧 STOP 依赖并显式加载当前 PowerShell 自带 Utility 模块；导出完整性代码和断言未改。

实施 session/revision/epoch 独立帧流账本、异步取消计数、真实/有效生成/Present 分离、GPU/blit epoch 过滤、采集 callback sequence 和 DLSS 采集 FG 提交前整对 deadline admission。跳过后的 FG 先 reset/reseed，不跨缺帧输出。另修关闭 FG（UI 值1）与内部倍率容量（至少2）误比较导致内容节奏设置被拒绝。详细文件、命令、失败及未完成项见 `docs/SCHEDULER_REPAIR_IMPLEMENTATION_2026-09-10.md`。

RTX5070 实际短测：Release exit0；CPU43/worker12 PASS；60->30->60 live23、FRUC保留路径18 PASS；DLSS2/3/4X 每项40真实帧和40 NR、skip8/16/24、Evaluate32/64/96、有效生成29/58/87、debug0；XeSS SDK generated43/debug0；专业UI24移动/24缩放/4弹出选择器 PASS；delivery23 PASS47.64秒（`13a7fe7290f643d29c64f6acc8fe8a32`）。均单次<300秒，没有打开实体采集卡。

同源原生4K NR+VSR最高+DLSS4X过载回放：150源帧观察点，旧策略450次FG计算/444过期/0生成呈现；新策略450提交前跳过/0过期/0生成呈现，处理约27->33fps。只证明该配置无效工作减少，不是NR自身加速或实卡/光子延迟测量。

FRUC试接admission造成重复worker重建、有效生成不增长，已从产品入口撤下该策略并复测通过。FRUC reset-pixels旧失败、完整S3/S4和实卡仍开放。首轮EXE占用链接失败、half-rate设置拒绝、FRUC反例日志均保留。本轮未发布或上传，源码与SDK/runtime隔离；下一任务为FRUC恢复像素/同步/时间戳定位。

续接审查补齐首批命令槽等待与文件播放取消账本，短测新增 worker hash 与独立日志目录。`scheduler-reviewed-build.log` 构建成功；`cpu-reviewed`43、`worker-reviewed`12、`live-half-reviewed`23、`live-fruc-reviewed`18项通过。FRUC reset 错图匹配上一真实帧或上一对插值，诊断性 cuCtxSynchronize+D3D11通知通过6个epoch像素检查，但相对时间、GPU事件、独立fence、参数生命周期和D3D11桥接候选仍失败，全部撤下；生产FRUC未变，不引入逐帧CPU等待。详见实施文档与 `logs/scheduler-repair-20260910/fruc-reset-*`。联合短测第一次23项功能全过但落盘找不到Get-FileHash整体失败，已修模块加载并重跑。下一条任务为FRUC CUDA/D3D11资源交接与完成可见性。

最终 `delivery-reviewed-fixed` 23项全部通过，外部42.78秒；`logs/delivery/ac24ad4a2e1e4169a171fda66e4b7e84/result.json`。当前EXE `429C5600E2A4ABF8D72B83B0F658B3190FE5397FE9A11256946B924CABA560F4`。diff检查通过，Git范围不含SDK/runtime/二进制。仅本地checkpoint，不push或发布；FRUC重置像素、完整S3/S4及实卡仍未完成。

# 2026-09-10 XeSS、AMD 光流与 SR 目标接入

用户要求把 AMD DLSS5 作为实验功能接入，并同时完成 XeSS FG、AMD 光流和 2K/4K/8K SR 目标。当前实现新增统一 SR 目标设置、专业模式 2K/4K/8K 选择、Intel XeSS 实验显示补帧 2X，以及 AMD FidelityFX 光流 provider 与半分辨率性能档。XeSS 只作用于预览，不能导出；AMD 光流只替换运动估算，不提供 AMD NR、DLSS SR、NVIDIA FRUC/DLSS 补帧、NVENC 或 AMD AMF。

专业模式第二页已按实际控件高度重排：补帧后端、光流后端、AMD 性能档、能力说明、光流档位、内容节奏和 FRUC 说明不再占用同一区域。此前新增说明文本与光流档位下拉框重叠，滚动时会影响可见性和点击范围；这是布局错误，不是渲染能力问题。

实际验证：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release` exit 0；`veyra_repair_contract_tests.exe` 36 项通过；`veyra_repair_preset_tests.exe .\logs\experimental-backends-final-1789031270387\presets.v1` 通过；`veyra_motion_validation_tests.exe` 的水平/垂直 confidence 及 D3D12 debug 检查通过。RTX 5070 上 `veyra_experimental_backend_tests.exe xess` 成功初始化、标记资源并 Present，`generated=43`、`debugErrors=0`；`... amd` 成功执行 AMD OF Create/Dispatch/Destroy，`amdDispatches=48`、`debugErrors=0`，且非周期纹理的 canonical `current -> previous` flow 在源图右移2像素时为 `(-2, 0)`、左移2像素时为 `(2, 0)`；`... sr2k` 3 次 DLSS SR Evaluate 并读回得到非黑 `2560x1440`，`... sr8k` 3 次 Evaluate 并读回得到非黑 `7680x4320`。每个测试进程少于五秒。它们不证明真实扫描输出帧率、画质、AMD GPU 兼容性、实卡效果、8K 实时性或长期稳定性。

没有提交 NVIDIA SDK/runtime、模型、头文件、库、样例或测试媒体；没有 push、Release 或 Runtime Pack 变更。Phase 7 仍为 `in_progress`。本地 checkpoint 前已复跑本轮构建和短测；仍须检查 Git 跟踪范围与差异。AMD 实机兼容、XeSS 实际显示节奏、FRUC reset 像素和原生 NR 全链路性能仍未解决。

# 2026-09-10 2K / 8K SR 与 AMD NR 研究补充

用户继续增加2K/8K超分和AMD显卡运行DLSS5的研究要求。按上一请求先写方案，交付 `docs/SR_TARGETS_AMD_NR_PLAN_2026-09-10.md`，同步XeSS方案/交接/loop研究记录。已确认Daniel HIP、RedDuke ZLUDA、Kien D3D12/HLSL三条真实技术路线；公开源码/作者自述/本机实测严格分开。推荐MIT D3D12网络作为RDNA4候选；其当前网络固定1080及SM6.10/FP8依赖、HIP原许可与无公开API、ZLUDA逐帧CPU等待与黑图问题均明确列出。社区4K60帖子混合FSR/FG，不能当原生4K NR60证据。

SR方案列出2560x1440、3840x2160、7680x4320，默认4K、等比、预设schema5、实时回滚、8K资源峰值与后端实际测试、大图不静默忽略SR、NVENC能力查询且保留既有完整性检查。AMD完整路线还须设备/NGX按需初始化、AMD OF、可选FSR1、XeSS与AMF，不能只换DLL。具体文件/顺序/来源/本轮命令见报告。

实际仅Git/rg/文件读取、gh api/固定raw文本、CIM硬件枚举与hash/签名；无构建、SR2K/8K或AMD Create/Evaluate、GPU/实卡/gate执行，无push/Release。RTX5070外还有9700X核显，不是已验证RDNA4目标。EXE仍E97B716B99116BEC942262FFEF1612299CBB2F4B0BDA7C308A5BFF318B3B5157；NR固定SHA一致/签名Valid。原UI工作树和FRUC失败保留，Phase7仍in_progress；下一实施任务S0共享SR目标与预设兼容。资料检索的404/大小写/不存在路径及一次未应用patch均记录在报告，不记GPU结果。

# 2026-09-10 XeSS / AMD 光流接入研究

用户要求针对“067内测：DLSS5+XeSSFG+AMD光流”先找方法写方案。新增 `docs/XESS_AMD_OPTICAL_FLOW_PLAN_2026-09-10.md`。固定 Magpie experimental ac1cc8b、Intel de0fb9c（Release v3.0.2）、AMD 60f4ea8 核对公开接口/代码及许可证；本机Magpie0.6.6已有XeSS DLL与标记文件，公开未查到0.6.7 Release。XeSS非Intel当前仅2X，必须XeLL和代理交换链；AMD OF为跨厂商compute，性能/质量档主要是宽高各半/全尺寸。平面深度、原生资源生命周期、双重调度、guidance每帧lease、导出接口限制及同配置A/B已写入方案。AMD成本不等于NVOF，也不证明NR自身推理变快。

只运行Git/rg/文件读取、gh api与官方raw文本下载、hash/签名核查；没有构建、新GPU效果/采集测试、SDK执行或产品改动，没有push/Release。上一轮UI改动保留，EXE仍E97B716B99116BEC942262FFEF1612299CBB2F4B0BDA7C308A5BFF318B3B5157。Phase7仍in_progress，所有新后端未实施；下一实施项是官方AMD OF同设备诊断及NR-only对照。本轮按用户要求停在可执行方案，不自动施工。

# 2026-09-10 原生 UI 闪白修复

控件原先只接管 WM_PAINT，状态更新同步绘制与 WM_PRINTCLIENT 可能露出原生外观。Theme/PopupSelector 已统一缓冲绘制并抑制模型更新期间的原生绘制，保留输入、隐藏及外部禁重绘状态。Release 构建、21项控件回归、14个菜单场景、实际RTX设置页、24次移动/缩放及三种FPS布局检查通过。首轮设置页固定1秒等待失败，改为限时等待实际尺寸日志后通过。命令、文件、真实Create/Evaluate统计和失败路径见 `docs/UI_PAINT_REPAIR_2026-09-10.md`。没有长时录屏、实卡或新增独立Reviewer验收；没有重跑delivery，不修改控制面、不发布。Phase7仍in_progress；下一步用户重启复核原先闪烁操作。

# 2026-09-10 当前修复与本地存档

新增一秒GPU完成FPS和采集60→30；同步此前颜色/异步呈现/Drop reset/实时光流/日志改动。文件、命令与边界见 `docs/HANDOFF_2026-09-10.md`。Release exit0；CPU41/worker11/预设PASS；真实engine half-rate19 PASS；UI日常/专业/窄窗口PASS；delivery23 PASS45.317秒，run `910fe667d2574babaa3b02a7aa9bb3e2`。未打开实卡，不是整体Phase7通过。

初次Windows min宏编译失败和UI辅助根窗口误认已修正，失败日志保留。用户纠正自然FPS变化发生在调画质期间，撤回对该段的确定异常归因。NR固定SHA与Valid签名核对。FRUC重置像素、临界负载节奏仍开放；不改控制面、不push。用户授权本地Git存档当前源码/文档，不含SDK/runtime/媒体/日志。

# 2026-09-08 优化 Goal 启动与 preflight 停点

用户明确开启目标模式，新增解除不合理媒体尺寸限制、专业预览悬停滚轮缩放，并实施现有优化方案；导出完整性检查保持现状。Goal 和 BACKLOG 已建立。

执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`，2.604秒 exit1，唯一失败 README hash。当前 README 与此前用户要求更新并上传的 f79ef95 一致；其他控制与两份二进制身份通过。按 AGENTS 明确停工条款，未自行修改控制清单/gate，已准备仅两处 hash 同步的未应用提案 logs/optimization-goal-20260908/，等待明确授权。

只读定位到 EngineController.cpp:89 对所有来源统一拒绝 3840×2160 以外和奇数尺寸，专业缩放需接 AppShell/VideoPresenter。新行为尚未实现；本轮未构建、未运行 GPU/实卡。STATE/JOURNAL/EVIDENCE/INBOX 同步，整体仍 Phase7 未验收。

---

# 2026-09-08 画质优化调研与 UI 修正像素检查

用户要求保持现有导出完整性检查，核对官方 / GitHub / 其他渠道的 NR、4K SR、FG 路线，以及当前 UI 修正是否有效，并合并旧方案给出优化建议。交付见 [优化方案](QUALITY_OPTIMIZATION_PLAN_2026-09-08.md)；架构审计加注最新决定，保留历史差距事实。本次不实施产品改动、不扩大导出检查，也不自动启动新后端或深度模型接入。

核对 NVIDIA DLSS5 研究说明、公开 DLSS / Streamline 文档、RTX Video SDK、NVOF / FRUC，以及固定提交的 Magpie、AIO、Feeder、video2dlssnr、Odyssey、2600th、Infinity Studio、Visual Enhancer，另参考 mpv / Video2X / RIFE 与教程和社区反馈。重点更正：NR 推理输入与 SR / FG 不能混同；本次 Magpie 最新源码使用 ZeroDepth，旧 DAV2 描述仅适用于历史版本；AIO 作者像素实验给出 UIAlpha / Backbuffer 资源线索，不能当成本机已验证的接口合同。

当前面板 `UI修正 · 未证实` 实际连接 `DLSSNR.UICorrection`，但 NR 未提供 UI / UIAlpha / Backbuffer / ControlMask，FG 也未提供其独立 UI 资源。自写隔离 probe 链接现有产品库，在 RTX 5070 上以静态 / 移动背景和固定文字 HUD 驱动真实 EnhanceGraph。自动遮罩开、关两种配置下，切换 UI 修正的四个取样帧 RGB 差异全部为零；重复基线也为零，而自动遮罩与强度零对照会改变像素。因此不能把当前选项当作有效的自动 UI 剔除；结论限定于所测内容和设置，不是任何场景永远无效的证明。未自动点击原生 UI 控件。

实际执行命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\logs\research\20260908\build-run-ui-probe.ps1`。最新诊断构建 2.684 秒、运行 4.600 秒、exit 0；192 次 NR Evaluate、180 次 NVOF Execute；Feature 18 Create `0x1 / Success`、非空句柄、SEH 0。最新证据位于 `logs/research/20260908/ui-1f853a0238804d8db3b26e3f763d7033/` 的 result JSON / stdout / CSV。诊断 EXE `0B0674016A029EAA81A0C6111EF3F895DFA601377D76A20FD6E204F13C62C405`；源码 `DBEE9D6D215ED37EC741CCE47515B75C154EB42E7877B953194EE6438C62DE35`。

失败与补测：首次程序产生完整输出但旧 Start-Process runner 没拿到 ExitCode，脚本失败，不算通过；改用显式 Process 结果读取后运行 4.230 秒 exit 0。看到 PNG 色块通道异常后增加 NR-off 与源图保存对照，得到上述最终 4.600 秒结果。三次进程均小于 300 秒，失败日志保留。CPU 源 RGB 245/128/38 经当前 PNG 保存后独立解码为 38/128/245；NR-off 内存为 244/128/36、保存后为 36/128/244，定位到保存路径。WIC 实际协商 GUID 尚未测得，因此仅将未处理格式协商列为疑似原因。UI 对照在 PNG 编码之前进行，不受该问题影响。

只新增方案、更新架构审计说明与本 WORKLOG；第三方文本缓存、自写诊断源码 / EXE / 图片与结果均在 Git 忽略目录。应用 EXE SHA256 仍为 `7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514`，指定 NR / add-on 身份一致，add-on 未加载。未修改产品、配置、runtime、SDK 或保护文件，未运行全部 delivery gate / 新 SR-FG 性能 / 实卡测试，未提交或上传。整体仍 `Phase 7 / needs_review`；旧尾帧 gate 差异不由本次专项解决。

文档检查：方案、审计与 WORKLOG 的本地链接 / 代码围栏 / 行尾空格检查通过，`git diff --check` 通过；Git 状态只有这三份文档，EXE 和诊断源码 hash 复核一致。PNG 的独立 System.Drawing 读取结果另存于最终实验目录的 `png-channel-check.json`。

下一条建议任务：先完成帧时长与颜色合同修复，再核验 SR 参数、实现确定有效的 UI 保护与运动可靠性增强；RTX Video SR 和深度先独立测收益。导出完整性检查保持现状。

---

# 2026-09-08 原始方案与当前架构审计

用户要求核对原始方案并对照当前架构。本地基线 `f79ef95`，工作树初始干净。完整对照见 [架构审计](ARCHITECTURE_AUDIT_2026-09-08.md)。确认真实产品复用 `pipeline::EnhanceGraph`，但完整帧 / 颜色 / reset 契约未贯通，深度推理与 C / 离线双向质量分支缺失，confidence 仅 cost 硬门控，播放器 / 导出主动选择软件解码；字幕、导出格式与完整验证也未达到原方案全部范围。

具体静态问题：采集 packet 未填 duration，默认 known zero 被调度器 clamp 到约 8.33ms；source 层与 graph 对缺失颜色标签的默认解释不一致。没有把这两项静态推导说成已测得画质或实卡延迟。报告单独列出后续已批准的分辨率 / 残差、源空间光流、深度暂缓、双模式 UI、独立导出进程和短测规则，避免混同为擅自偏离。

本次仅运行 Git / rg / 文件读取及文档检查；只改审计文档与本记录。未构建，未执行 RTX / NGX 或实卡测试，未改变应用、EXE、保护文件与 runtime，也未提交或上传。整体仍 `Phase 7 / needs_review`。下一条建议任务：贯通真实产品 FramePacket，先修采集 duration 与颜色元数据的唯一解释；本次尚未开始实现。

---

# 2026-09-08 GitHub 源码存档准备与 README

用户明确要求建立 Git 存档并上传到 `Likely7/Veyra-DLSS-Video-Player`。本次只修改 README、保留远端既有 LICENSE，并补充存档记录；应用源码与最终 UI v4 EXE 未改，不重新运行构建、GPU 或实卡测试，也不改变 Phase 7 / needs_review。

初始本地 `c557d5f`，94 次提交、661 个历史 blob、248 个当前文件。只读检查发现历史包含已移除的测试 MP4 和 `third_party_local/depth/manifest.json`，当前树没有这些文件。全历史常见密钥模式扫描未发现匹配，不把模式扫描当作绝对安全证明。指定 NR / add-on 身份匹配，Lucide 素材与许可保留。

远端 `main` 初始为 `8556fc7`，只有 README 和 GPL v3 LICENSE。沿用用户仓库已有许可证；保留本地完整历史，在远端初始提交之后追加当前源码快照，不合并或推送本地旧历史。详细范围与后续同步注意事项见 [源码存档记录](SOURCE_ARCHIVE_2026-09-08.md)。上传后的提交身份以 GitHub 远端及本机 `logs/github-archive/` 核对记录为准。

本轮执行 Git 状态 / 历史对象检查、`gh auth status`、`gh repo view`、`git ls-remote` / `fetch`、README 本地链接与格式检查、源码树 / 许可证 / 图标身份检查；结果记录在 `logs/github-archive/`。原始日志、SDK、runtime、用户配置和媒体不随源码上传。原有统一 delivery 门禁差异、实卡体验与长稳仍未解决；本轮不新增产品通过结论。

---

# 2026-09-08 UI v4：常用操作直出与统一玻璃选择器

日常底栏直接提供打开、采集、最近、总增强、SR、播放、音量、字幕、全屏及窗口控制；720px保留全部入口。采用固定来源的Lucide免费图标（完整ISC/Feather MIT通知），字幕和所有应用内下拉框使用同一Desktop Acrylic弹层。保留真实桌面透明与纯黑视频区域。

基线 `97ccd19`；最终EXE SHA256 `7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514`。最终布局/14场景弹层专项1.312秒PASS；40次切换15.008秒PASS；原生NR/SR/NaN草稿/底栏可见性25.858秒exit0，Create/Evaluate实际成功。紧邻的12DIP排版微调前，全UI15命令141.503秒PASS，图片/视频/导出专项11命令80.914秒PASS。报告明确区分EXE版本，不把旧证据冒充最终抓图。单次测试均小于300秒。

实际检查宽窗口、720px窗口、字幕/专业下拉、键盘确认取消。独立只读复核通过其限定代码范围；最后SR文字宽度微调另经主Agent验证。保护文件和runtime身份不变，整体仍Phase7/needs_review，实卡、长稳、分发和既有delivery门禁失败不由本轮UI验收代替。

详细文件、命令、真实日志、失败与截图：[UI v4交付报告](UI_CONTROLS_V4_DELIVERY_2026-09-08.md)；[只读复核](REVIEW_UI_CONTROLS_V4_2026-09-08.md)。下一条唯一任务：用户验收底栏/选择器，并继续原合同的实卡体验验收。

---

以下为历史记录：

# 2026-09-08 UI修复v3：真实桌面毛玻璃

已删除应用内彩色渐变，控制区采用Windows Desktop Acrylic，透出下方桌面或其他窗口的模糊颜色；视频区域不透明、空闲纯黑。同步修复窗口白边、视频全屏、专业NR/SR开关、数值草稿/回滚、滚动裁剪、日常底部布局与240ms展开动画。独立采集对话框仍为普通深色。

基线`b6b251d`，最终EXE SHA256 `B1892C51E8AFC27D7223E271D48D93107C34CAA96BC4014DFF6F45FDE0D7BF74`。build16成功；专项11命令PASS/81.001秒，UI全套15命令PASS/141.187秒，Repair18项PASS/子进程70.478秒，当前4K合同23项PASS/13.751秒。均为每次300秒以内，历史累计保留。40次切换P95 8.191ms/max9.963，442次提交epoch1，GDI12/handles721稳定。真实红蓝背窗、视频字幕、输入/粘贴、全屏及滚动已检查；独立只读复核通过本轮范围。

全项目仍Phase7/needs_review；本轮保护delivery gate保留FAIL（旧23帧断言，当前12源帧2X含尾部CFR占位输出24帧），不修改保护门禁或用补测冒充阶段通过。实卡、真实IME候选、多屏DPI、长稳及公开分发未验收。没有开启采集流或公开上传，没有再次执行历史的一次性关机请求。

完整文件、命令、RTX Create/Evaluate日志、失败和截图：[UI修复v3报告](UI_REPAIR_V3_DELIVERY_2026-09-08.md)；[只读复核](REVIEW_UI_REPAIR_V3_2026-09-08.md)；[操作指南](USER_GUIDE.md)。下一条唯一任务：用户通过`Veyra.cmd`验收当前UI和原合同中的实卡体验。

---

以下保留历史记录，旧的“当前版本/通过状态”不代表本轮：

# 2026-09-08 双模式UI本机软件交付

已实现批准的UI0–UI9：默认日常影院界面、专业四区工作台、共享视频宿主与无重开切换、完整参数/预设、真实音量和透明字幕、后台冻结设置导出、诊断/全屏/小窗口。最初普通Win32排布在用户反馈后重新实现。

基线本地存档`ddc515d`；当前EXE SHA256 `2DE33230CD3A6556BC8E1399DD93BB8959B8EF37B2BACEF23D1486590AA4D3C6`。UI全套15子检查109.583秒PASS；Repair v2 18项PASS，子进程计时合计70.712秒；4K补充23检查14.178秒PASS。真实4K输入/底图/光流/FG/输出、1080内部NR短测59.55源fps、落后P95 1.77ms。每次调用最多300秒，累计历史保留。

受保护delivery脚本此次34.736秒后在旧23帧断言FAIL；基线已有CFR tail hold，当前12源帧2X正确输出24帧/120fps/0.2秒。保护文件未改，补测不替代Phase gate。全项目仍needs_review；当前UI软件交付不等于实卡、多屏物理DPI、长稳、完整组合性能或公开发行通过。

详情、文件、命令、错误码、截图和复核范围：[双模式交付报告](UI_DUAL_MODE_DELIVERY_2026-09-08.md)。操作：[使用指南](USER_GUIDE.md)。下一条用户验收：从Veyra.cmd启动，在真实采集卡上检查新界面切换、声音和体感延迟。本轮未开启采集流、未更改外部应用或运行时、未上传发布。

---

以下保留历史版本记录，旧的“当前EXE/尚未施工/已通过”不代表本次状态：

# Veyra Worklog

## 2026-09-08 Dual-mode UI execution document only

User approved the two-mode design based on their references: Daily default for video/capture; Professional for fine controls, comparison, telemetry/diagnostics and export. Added docs/UI_DUAL_MODE_EXECUTION_PLAN_2026-09-08.md with verified code map, shared-session invariants, responsive DIP layout, controls/shortcut rules, UI0–UI9 tasks and test matrix. Read-only inspection identified necessary backend work: current startExport replaces the playback worker and audio volume is not exposed. The plan explicitly includes a bounded independent export worker using shared product libraries and real per-application audio controls; these are planned, not claimed implemented. User-rejected design skill was not used.

No app code, runtime, protected gate, user media or EXE changed; no build/GPU/capture test, Goal, commit or upload. EXE remains 1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266. Static checks: document links exist, code fences balanced, UI0–UI9 present, 7 protected hashes unchanged, deleted fixture remains absent. Test limit remains per invocation <=300 seconds. UI status is design_approved / implementation_not_started. Next implementation task, when requested: UI0 inventory and UI1 shared state/stable video HWND.

## 2026-09-08 Repair v2 final known-fix review

User corrected test budget to per invocation <=300s. Implemented timestamp-quantization CFR validation and candidate selection, full Desc rollback, failure-code diagnostic capture, and cached source identity/count preservation. Final build exit0 (3.9694163s); joint joint-a077b89fd39348e4a49f4195c9d4e416 18 cases exit0 in 71.0278864s runtime. Historical cumulative 346.3985650s includes failures; not reset. Read-only fresh reviewer review_known_fixes passed this limited fix scope, verified EXE 1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266. No old Phase pass updated; global needs_review retained. Full evidence, modified files, intermediate failures, performance limitations and user capture next action: docs/REPAIR_V2_DELIVERY_2026-09-08.md. Protected hashes unchanged, runtime identity matches, no capture/commit/upload. SR4K+4X smoke39.75sourcefps/2.17s lateness remains an explicit performance limitation, not a functional transaction failure hidden as realtime pass.


## 2026-09-07 B1–B5 selected; planning/handoff only

User selected B1–B5 for the next implementation scope: same-frame comparison, per-stage performance UI, transactional user presets, shared optical-flow quality profiles, and a local privacy-aware diagnostics center. The repair plan now contains their precise data ownership, state transition, UI, failure and acceptance contracts; the Magpie backlog marks them selected. Added a paste-ready next-conversation handoff that explicitly refuses to treat the historical shared Phase5–7 gate as new release proof and forbids modifying protected hashes/gates to manufacture a pass.

This entry is documentation only. No application code, gate, CONTROL_HASHES, review prompt, SDK/runtime, driver, external app, capture device or user media was changed or executed. No build/test/Goal/checkpoint/publication was performed. Current software remains needs_review with SR4K/FG user-visible defects unresolved.

## 2026-09-07 SR4K / FG / parameters planning only

User requested detailed documents and a Magpie feature shortlist before implementation. Added `C:/Users/123/Desktop/Veyra DLSS Video Player/docs/REPAIR_EXECUTION_PLAN_2026-09-07.md` and `C:/Users/123/Desktop/Veyra DLSS Video Player/docs/MAGPIE_FEATURE_BACKLOG_2026-09-07.md`; updated DELIVERY_STATUS with the unresolved SR4K/FG feedback and planning links. The plan covers resolution separation, real generated-frame ownership/content/pacing, SDK MFG 2/3/4, export timing, typed NR controls, independent residual controls, live settings, Chinese/fullscreen UI, and a shared 300-second future runtime-test budget. Other competitor features remain user-selectable candidates, not automatic implementation tasks.

Evidence this turn: read-only current code inspection plus Magpie 0.6.6/0.6.5 release and parameter/frame-sync documents; no Magpie GPU benchmark. No application code, SDK/runtime, protected control file, control hash, user configuration or existing deleted fixture was changed. No build, GPU test, app/device operation, Goal, checkpoint, driver/remote-software operation or publication. Documentation static checks are recorded with this turn's tool results; prior EXE and needs_review state remain unchanged. Next action: user selection/implementation confirmation, then resolve protected-document scope before any new Goal.

Static verification: `git diff --check` exit 0 (existing LF/CRLF notices only); all absolute local Markdown links in the two new documents and DELIVERY_STATUS resolve; no Unicode replacement characters; all 11 protected manifest file hashes match. No gate was run and no manifest was edited.

## 2026-09-07 capture latency repair

User explicitly requested implementation after the physical-card diagnosis. Owned bounded capture AVFrames + deferred Run + live-specific bounded pacing remove the stale-PTS wait; ring/presenter use one rotating cursor; live metrics no longer masquerade as photon latency. Code/files/commands/results are recorded in `docs/CAPTURE_LATENCY_FIX_2026-09-07.md`. Release build exit0; timing7/7, source8/8, physical 1080p50 off/NR/NR+FG ~49.4–49.8fps and0drops; final NR195/NVOF193/generated193. Visible-player consolidated gate21/21 exit0 in47.309s, run01b72df768524af9ab0aa8d2e3dbb59e, final EXE4A9BA4B321DEEC17C5E3562AF03EF75A316856200A8708FA8E692475A05B59C9. Preflight71/71. No proprietary files, external apps, driver, user settings or deleted user clip changed. Read-only reviewer attempt failed without final verdict; needs_review, no new checkpoint. User perceived latency/audio and actual generated-frame display cadence remain unverified.

## 2026-09-07 direct implementation delivery

See docs/DELIVERY_STATUS.md for current software, tests and limits. Actual app/controller/presenter, DirectShow source, WIC and D3D12 NVENC export now exist; earlier “no UI/export” entries are historical. Fixed NV12/uint shader inputs, real guidance-before-NR, scene resets, audio format/paused seek, GPU timestamp semantics. CMake x64-release exit0; consolidated gate run d28879b01b5c44dd86cad33d6f386d90 exit0 in31.59s. No 30-minute retests. User accepted realtime internal-resolution option after GPU measurement. Independent review next; do not claim phase checkpoint yet.

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

## 2026-09-02 Phase 2 — RenoDX-equivalent parity codec (gate 35/35 + reviewer PASS)

Goal:

Implement the parity codec end to end: CPU golden reference (Playbook §9 exact math), ParityEncode/ParityDecode HLSL compiled at build time, the harness --parity-compare mode (Original FP16 → encode → 16 Feature-18 evaluates → decode → Final FP16), GPU-vs-CPU statistics, four-stage captures, and the phase2 gate.

Changed:

- include/veyra/parity + src/parity: RenoDxParityCodec (shoulder 0.75/5.7780, sRGB, six OkLab/AP1 matrices in mul(matrix,vector) direction, signed cbrt, HueOkLab, UpgradeToneMap two-stage, luminance-only).
- tests/unit/ParityCpuReference.cpp: 12 golden checks (threshold continuity, neutral bypass identity, highlight luminance restoration, quantization bounds).
- shaders/Parity{Encode,Decode}.hlsl + cmake shader targets; tools/nr_harness parity_compare mode + shared harness_util (PNG writer, JSON, stats).
- scripts/gates/phase2.ps1: CPU tests both configs, GPU-vs-CPU tolerances, four-stage captures, neutral baseline + addon hash, raw≠final.

Commands actually run (key evidence):

- veyra_parity_tests: 12/12, 0 failures in both configs.
- --parity-compare: encode maxCodeDelta=1 (≤1), maxAlphaDelta=0; decode beyondOneUlpCount=0, maxAbsError=0.00390625 (= exactly 1 FP16 ulp at [4,8)), nanInf=0; infoqueue stored=0 errors=0 (debug run persisted).
- loop-gate -Gate phase2: 35/35 checks exit 0 (reproduced identically by the reviewer in an independent run).

Reviewer outcome (P2.6):

- First review: FAIL with 1×P1 (an evidence line about the debug parity run had no persisted artifact — same class as the Phase 1 P1) + 6×P2.
- Fixes: unconditional infoqueue drain with counters into the JSON, a real persisted debug run, RNE float→half (matching GPU storage), alpha comparison, stage luma statistics into the JSON, stage JSON enriched with rowPitch/runtimeSha256/pts/source.
- The RNE fix surfaced a real physical effect: highlight-amplified fp32-vs-double intermediate differences cross FP16 bucket boundaries (5038 of 2M pixels, every one exactly 1 ulp; bit-level examples in the log). The absolute 0.002 bound is mathematically unreachable at ≥4.0 for any correct fp32 pipeline, so the gate enforces diff ≤ max(0.002, 1×stored ulp) — the reviewer examined the worst-pixel evidence and accepted this ruling as the same-intent bound (0.002 verbatim below 4.0).
- Final review: VERDICT PASS (three-way reproducible numbers, control plane untouched from the Phase 1 checkpoint). Three one-line P2s fixed immediately; two P2s filed for Phase 3 (--profile parsing, in-flight parameter-block reuse hardening).

Decision:

- All parity math comes from Playbook §9; every tolerance kept falsifiable; every claim backed by a persisted artifact.

Next single task:

Phase 3 P3.1: vcpkg/FFmpeg 310-baseline dependency acquisition + phase3 gate (fail-closed).


## 2026-09-02 Phase 1 — Feature 18 native harness (gate 54/55, one user-action item)

Goal:

Build the Feature 18 harness end to end: NGX core host, isolated caller-name compatibility layer, signed-snippet Create/Evaluate, deterministic test-pattern Proxy, 300-frame runs with statistics/captures/variants, and the phase1 gate.

Changed:

- include/veyra/ngx + src/ngx: NgxCoreHost (single Init/Shutdown, parameter-block lifecycle, SEH), ParameterBlock typed setters, DlssNrParameters constants, DlssNrRuntimeAdapter (restricted load, 5 exports, PE-import IAT shim with single-owner install/restore, SEH-wrapped snippet calls, scaling-ratio callback).
- shaders/GenerateTestPattern.hlsl + cmake/VeyraShaders.cmake: build-time DXC compile (deterministic quadrant pattern with frameId shift).
- tools/nr_harness: --load-only/--shim-test/--create-test and the full frame loop (Proxy->Feature18->Raw, zero guidance via upload-copy, 4-slot execution, PNG captures, statistics, variant segments, GPU timestamps, gate-contract JSON).
- NgxResult: full official 310.7 result table (Success=0x1 — corrected from an earlier wrong assumption).

Commands actually run (key evidence):

- Core Init_with_ProjectID result=0x1; snippet Init_Ext (AppID 0x0876232C) result=0x1; CreateFeature id=18 result=0x1 handle non-null; Release/Shutdown results all 0x1.
- Shim boundary battery: 8/8 PASS (zero-size, truncation with ERROR_INSUFFICIENT_BUFFER, exact 10-wchar, roomy, nullptr/other-module forwarding, restore verified).
- 300/300 Evaluate succeeded in BOTH Debug and Release (0 failures), output meanLuma≈0.494 stddev≈0.327 non-black non-constant; three distinct hashes (baseline / style=1 / intensity=0.5); GPU timestamps non-zero, avg ≈6.3 ms/frame at 1080p.
- Lifecycle: two consecutive full runs + create-test + shim-test 4/4 PASS.
- loop-gate -Gate phase1: **54/55 checks PASS; the single FAIL is json-debug:debug-layer-enabled (debugLayer=False)**.

Artifacts/logs:

- logs/phase1/824a66eca2bd4ebfb23a28950eecbc8f/ (gate runs), logs/tmp/p16*.json/out, captures (gitignored).
- third_party_local/nvidia/DLSS_SDK_310.7.0 staged from the official GitHub repo clone (headers + nvsdk_ngx_s[_dbg].lib + rel DLLs; nvngx_dlss.dll BE6E434A…, nvngx_dlssg.dll 135EAF07…).

Failures and exact codes:

- ClearUnorderedAccessViewFloat crashed (139) during zero-init even after binding heaps; replaced with an upload-buffer copy path (equally deterministic). Root cause unverifiable without the debug layer; noted for re-check after Graphics Tools is installed.
- Compile iterations: SDK header include order (d3d12.h before nvsdk_ngx.h), Init_with_ProjectID casing (capital D), NVIDIA static libs are MT-flavored (switched tools to static CRT via CMP0091 + per-config _dbg lib), DXC argument quoting via generator expressions (switched to CMAKE_BUILD_TYPE branch), union aggregate init.

Decision:

- NGX result table and all signatures come from the staged official 310.7 headers, never memory.
- Zero-init via upload copy; JSON debugLayer reports the actual runtime state.

Next single task (user action required):

~~Install Windows "Graphics Tools"~~ (user installed 2026-09-02; probe verified "d3d12 debug layer enabled").

Reviewer outcome (P1.8, 2026-09-02):

- First review: VERDICT FAIL with 1×P1 — the gate's no-state-errors grep was vacuous because no component captured the debug layer's OutputDebugString stream; plus 5×P2 (literal log line, path containment, SEH on parameter calls, hardcoded nanCount, 10-frame debug matrix).
- Fixes landed (commit ff98e37): real ID3D12InfoQueue capture in debug builds (attach after device creation, drain after the full loop into the log and a debugInfoQueue JSON block), gate now asserts infoqueue-active and no-error-messages (both falsifiable), debug run raised to 30 frames, exact Playbook 8.1 literal, runtime_local/nvidia containment check, SEH wrappers for Allocate/DestroyParameters, nanCount removed.
- Final review: VERDICT PASS (independent fresh-build run: release 300/300, debug 30/30 with infoQueue active and 0 error messages; three parameter-variant hashes identical across four independent runs; anti-stale runId/exeSha256 verified; control plane untouched from the Phase 0 checkpoint). Three non-blocking P2 residuals recorded in the journal (teardown-time infoqueue drain, suffix vs prefix containment, 200-message drain cap).

Phase 1 conclusion: gate 56/56 + reviewer PASS. Phase 2 unlocked.


## 2026-09-02 Phase 0 — Runtime probe + D3D12 skeleton

Goal:

Complete Phase 0 per the Playbook: fail-closed phase0 gate, minimal CMake/C++20 project, veyra_base (logger/result strings/file identity), veyra_gfx (D3D12DeviceContext + 4-slot ring), full veyra_runtime_probe, and the real gate run including the 5-minute window loop.

Changed:

- Initialized local Git per LOOP_ENGINE fixed order (baseline 2086282, branch agent/veyra-v1-loop, loop pointer commit 09abf5d).
- Added scripts/gates/phase0.ps1 (fail-closed, verified failing before the project existed).
- Added CMakeLists.txt/CMakePresets.json/cmake/VeyraWarnings.cmake (Ninja x64 debug/release, /W4 /permissive- /WX).
- Added scripts/build.ps1 (vswhere/vcvars resolution; no machine paths in presets) and scripts/stage-runtime.ps1 (pinned-identity copy + manifest + persistent ngx-local.json).
- Added include/veyra + src/base (Logger, Status/HRESULT/NGX strings, BCrypt SHA-256 + WinVerifyTrust + signer extraction) and src/gfx (D3D12DeviceContext, CommandSlotRing with timestamp heap).
- Implemented tools/runtime_probe/main.cpp: --self-test, --device-info, full mode (restricted LoadLibraryExW, 5 exports, nvofapi64 probe, fixed-size window + flip swapchain + 4-slot loop, JSON summary with runId/exeSha256).

Commands actually run:

- preflight (54 checks then 66 after Git): exit 0 both times.
- Toolchain/GPU probes: RTX 5070 / 616.56 / 12227 MiB / compute 12.0; nvofapi64 32.0.16.1656; MSVC 14.44.35207; CMake 3.31.6; Ninja 1.12.1; DXC 1.8; Git 2.53.
- scripts/build.ps1 -Preset x64-debug / x64-release: exit 0 (multiple times).
- veyra_runtime_probe --self-test / --device-info / full smoke: exit 0 each.
- loop-gate.ps1 -Gate phase0: three honest failures (locale version format; SwitchParameter binding via -File; ignore probe on non-existent dirs), each fixed without lowering thresholds, then **exit 0: VEYRA GATE PASSED: phase0 (70 checks)**.

Results:

- Gate run-id a5fd6348b3084b44857b1f1ffc96a449 (308.3 s): exports 5/5; Debug window 3 s / 303 frames; Release window **300 s / 30002 frames, deviceRemoved=false**; staged runtime identity matches the pinned contract; git ignore 7/7; no sensitive files tracked.
- Driver version resolves via registry nvlddmkm.sys file version (32.0.16.1656); DisplayVersion value absent on this driver.
- D3D12 debug layer unavailable on this machine (0x887A002D, Windows "Graphics Tools" optional feature missing); recorded in loop/INBOX.md for user action before Phase 1.

Artifacts/logs:

- logs/phase0/a5fd6348b3084b44857b1f1ffc96a449/ (probe logs + JSON, gitignored)
- runtime_local/nvidia/{nvngx_dlssnr.dll, runtime-manifest.json}, runtime_local/config/ngx-local.json (gitignored)

Failures and exact codes:

- Gate iterations: runtime:fileversion "310,8,0,0" != "310.8.0.0" (locale) → FileVersionRaw; build exit 1 via ParameterArgumentTransformationError ("-Clean:$false" as string) → omit switch; git check-ignore exit 1 for non-existent trailing-slash dirs → in-directory probe files.
- Compile iterations: C4838 (DXGI literals), WinVerifyTrust const GUID*, namespace log::, wchar→char C4244, IDXGIAdapter1 vs DESC3, ComPtr .Get() for Signal, GetCurrentBackBufferIndex needs IDXGISwapChain3. All fixed; no warnings remain (/WX).

Decision:

- Keep machine-specific paths out of tracked files (build.ps1 resolves them); gate verifies staged state rather than staging itself; probe JSON embeds runId + exe SHA-256 so stale artifacts cannot pass.

Reviewer outcome (P0.9):

- Independent read-only sub-agent reran preflight (exit 0) and phase0 (exit 0, its own run-id a55d5fb41b164abc88fc2760f0b635ec, 300 s / 30003 frames), verified BASE_COMMIT, the full diff (control plane untouched), anti-stale runId/exeSha256 mechanics, and reverse-order cleanup. VERDICT PASS, zero P0/P1.
- Four P2 hardening notes recorded in loop/JOURNAL.md Cycle 009; the fence-timeline ownership item is queued as BACKLOG P1.0a; D3D12 debug-layer absence remains in loop/INBOX.md for the user before Phase 1's debug-layer criterion.

Next single task:

Phase 1 P1.1: phase1 gate + deterministic RGBA8 test frames/output statistics (after P1.0a fence ownership hardening).


## 2026-09-01 Handoff baseline

Goal:

Prepare an implementation contract for the next Agent. No player source has been implemented yet.

Changed:

- Added `AGENTS.md` with project guardrails and phase gates.
- Added `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` with dependency acquisition, runtime layout, exact Feature 18 parameter contract, parity math, media pipeline, latency rules and phase acceptance criteria.
- Added `.gitignore` before repository initialization so local NVIDIA/RenoDX binaries cannot be added accidentally.

Commands actually run:

- Inspected the project file list and product-spec headings.
- Calculated/verified both local binary identities and Authenticode status.
- Inspected exported/runtime strings and the embedded RenoDX parity shader behavior.
- Verified the installed Windows, RTX 5070/616.56 environment and local Visual Studio/CMake/Ninja/DXC tool paths.
- Checked pinned upstream DLSS, Magpie, FFmpeg/vcpkg and NVOF references.

Results:

- Current repository state before handoff: no `.git` directory and no application source.
- Next allowed implementation phase: Phase 0 only.
- No DLSS Feature was invoked and no runtime test was claimed in this handoff task.

Artifacts/logs:

- `AGENTS.md`
- `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`
- `VEYRA_PRODUCT_SPEC_V1.md`

Failures and exact codes:

- None. One documentation patch wrapper parse error occurred before any write; it was corrected and had no workspace effect.

Decision:

Use direct NGX/D3D12 for V1; signed DLSSNR adapter is local-only; RenoDX add-on is reference-only; start with a native fixed-frame harness before the media player.

Next single task:

Execute Phase 0 from the playbook and stop at its acceptance gate.

## 2026-09-01 Unattended Goal Loop handoff

Goal:

Turn the implementation plan into a recoverable Goal-based loop that another Agent can run without phase-by-phase supervision.

Changed:

- Added loop/LOOP_ENGINE.md with single-writer state machine, evidence rules, retry bounds, independent review and stop/complete conditions.
- Added loop/GOAL_PROMPT.md as the copy-paste Goal task and loop/REVIEW_PROMPT.md as the read-only phase review task.
- Added persistent STATE/BACKLOG/JOURNAL/EVIDENCE/INBOX files.
- Added scripts/loop-gate.ps1 and scripts/gates/README.md.
- Updated AGENTS.md, the Playbook and .gitignore for unattended execution.

Commands actually run:

- powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
- powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase0
- PowerShell AST parse of scripts/loop-gate.ps1 and ConvertFrom-Json validation of loop/STATE.json.

Results:

- Initial preflight found a Windows PowerShell 5.1 source-encoding bug in a non-ASCII product-plan filename check (exit 1); the bootstrap check was made encoding-safe. The later audit renamed the canonical product spec to an ASCII path.
- Final re-run at 2026-09-01T17:16:32+08:00 passed 45/45 baseline checks (exit 0), including both local binary identities, STATE ledger/limits, ignore rules and eight protected control-file hashes.
- Negative phase0 test failed closed as intended (underlying gate exit 1): Git is not initialized and scripts/gates/phase0.ps1 does not yet exist.
- No application code, NGX Feature, video pipeline or Phase gate was claimed complete.

Next single task:

Start the Goal with loop/GOAL_PROMPT.md. The Agent must rerun preflight, finish P0.1 toolchain/GPU evidence, then execute Phase 0 in backlog order.

## 2026-09-01 Full project audit and cleanup

Goal:

Re-audit the whole handoff package adversarially, remove superseded documentation, repair contradictory implementation instructions, and leave the unattended loop fail-closed for a weaker Agent.

Changed:

- Added `README.md` as the canonical entry point and renamed the retained product boundary to `VEYRA_PRODUCT_SPEC_V1.md`.
- Deleted the obsolete V3 plan. It prescribed the superseded quality-first/capture/depth route and had no remaining active references.
- Removed stale Phase 8 and old-plan routing. V1 is strictly Phase 0 through Phase 7.
- Corrected the false premise that the RenoDX `.addon64` is a ReShade configuration. No preset exists in this workspace; Phase 2 uses a declared neutral codec baseline and only performs external-reference comparison if a matching preset/capture is later supplied.
- Removed the D3D11VA/D3D11On12 side route and aligned the minimum codec/container matrix with Phase 3/7 gates.
- Fixed the SR/NVOF circular dependency: V1 SR uses Zero Guidance; full-resolution NVOF is generated after SR and is shared only by NR/FG.
- Added the D3D12VA texture-array slice/plane/lifetime contract, the NVOF ABGR8 input/ring/reset contract, and exact `GetModuleFileNameW` shim edge semantics.
- Recorded the official DLSSG motion-normalization rule and isolated Magpie's conflicting `{1,1}` behavior as a diagnostic-only mode with a deterministic Phase 6 translation gate.
- Hardened `scripts/loop-gate.ps1`: exact nine-file control set, stronger STATE phase/evidence/bound checks, Git commit-pointer checks, representative ignore probes, current-phase enforcement, and before/after hashes that prevent a phase gate from mutating non-ignored project files.
- Rebuilt `loop/CONTROL_HASHES.json` for the canonical control plane.

Commands actually run:

- Official-source checks against NVIDIA DLSS SDK 310.7 headers, NVIDIA Optical Flow documentation/sample behavior, FFmpeg D3D12VA headers, the pinned vcpkg ports and Microsoft `GetModuleFileNameW` documentation.
- PowerShell AST parse, JSON parse for every JSON file, Markdown fence-balance scan, stale-reference/TODO scans, and file inventory checks.
- In-memory unit exercise of `Get-ProjectSourceSnapshot` / `Compare-ProjectSourceSnapshot` without changing disk files.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Negative gates for `phase0` and out-of-order `phase1`.
- Reversible negative STATE tests for `cycle.completed=80` with active status and for prematurely unlocking Phase 1; the original state was restored after each test.

Results:

- Audited preflight passed 54/54 checks, including nine protected control hashes and both local binary identities.
- `phase0` failed closed because Git is not initialized and `scripts/gates/phase0.ps1` does not exist; no Phase completion was claimed.
- `phase1` additionally failed the current-phase check.
- The loop-bound and phase-sequence mutations each made preflight exit 1 on the intended check, then the valid STATE was restored.
- Mutation-snapshot unit exercise saw 17 control/state files, reported zero changes for identical snapshots, and detected an in-memory README hash change.
- Current project truth remains: no Git repository, no application source, Phase 0 not started.

Deleted/renamed:

- Deleted the obsolete V3 plan. This workspace has no Git history yet, so that deletion is not recoverable from this directory's repository history.
- Renamed the retained product plan to `VEYRA_PRODUCT_SPEC_V1.md`; its useful content was audited rather than discarded.

Next single task:

Start the Goal using `loop/GOAL_PROMPT.md`. The first Agent action is the 54-check preflight; then it completes P0.1 and proceeds through Phase 0 backlog order without crossing the gate.

## 2026-09-03 Fast-track V1 scope rebaseline and competitor audit

Goal:

Replace the obsolete player-only route with the user-confirmed first-release scope: physical capture-card enhancement, interactive media player, and image/video export, all sharing one DLSS quality graph. Audit Magpie and recent GitHub competitors before changing the plan.

Facts found:

- Magpie commit `289dc0f6d52075f5a06b47a3f70b35d438095bf5` already contains NVOF motion/confidence, optional Depth Anything V2 Small with temporal reprojection, and DLSSG. Adding a depth texture alone is not a competitive advantage.
- `Merserk/dlss5-visual-enhancer`, `DaniilSokolyuk/video2dlssnr`, `Zonnery/dlss5-nr-player`, `SamG-Coder/dlss5-infinity-studio`, and `jlrouzies-fr/DLSS5-Feeder` were inspected at source/README level. Details, commit IDs, limitations and licenses are in `docs/COMPETITOR_AUDIT_2026-09-03.md`.
- The screenshot comment's useful lesson is the complete DLSS render contract, not reverse engineering itself. HDMI/video pixels cannot recover engine-native depth/motion/HUD-less buffers; Veyra will use estimated guidance and label it honestly.

Changed:

- Replaced `VEYRA_PRODUCT_SPEC_V1.md` with the three-workflow product definition and measurable Definition of Done.
- Replaced `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` with explicit module layout, dependencies, API contracts, motion/depth/reset rules, source/sink implementation steps and Phase 5–7 gates.
- Added `docs/COMPETITOR_AUDIT_2026-09-03.md`.
- Updated `README.md`, `AGENTS.md`, Goal/Loop/Reviewer instructions, gate contract, BACKLOG, STATE and INBOX.
- Expanded the protected control set from 9 to 10 files and rebaselined `loop/CONTROL_HASHES.json`; `scripts/loop-gate.ps1` still fails closed on any drift.
- Preserved Phase 0–4 checkpoints. Invalidated only the old Zero-only Phase 5 evidence because it no longer proves the new quality core.
- Adversarial re-read found and fixed one graph-order contradiction: V1 now states everywhere that SR uses Zero Guidance first, then NVOF/depth/confidence are generated at the post-SR `workingExtent` for Feature 18 and FG. This prevents a weak Agent from building an SR↔NVOF circular dependency or mixing resource extents.
- Verified the local official SDK already contains a signed `nvngx_dlssg.dll` 310.7.0.0 and recorded its exact path/size/hash in the Playbook. It is a Phase 6 staging source, not evidence that DLSSG currently works in Veyra.

Commands actually run:

- Cloned/fetched the six upstream repositories into a unique directory under `%LOCALAPPDATA%\Temp` and inspected files with `rg`/`Get-Content`; no upstream source was copied into Veyra.
- `git status --short --branch`, `rg --files`, JSON parsing, suspicious-text scan, `git diff --check`.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight` → exit 0, 68/68.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root <project> -Preset x64-release` → exit 0, Ninja no work to do.

Runtime result:

- This was a control-plane/research task. No new Feature 18, NVOF, DAV2, DLSSG, capture-card or export run was executed. Historical Phase 0–4 runtime evidence was not re-labeled as current product completion.

Known blockers:

- Optical Flow SDK 5.0 headers/sample require the user to accept NVIDIA's EULA and place the SDK under `third_party_local`.
- The final capture gate needs a real DirectShow/UVC device and HDMI test signal.
- Public distribution of NVIDIA runtime/model/FFmpeg assets remains unauthorized.

Next single task:

P5.1: replace the obsolete Zero-only `scripts/gates/phase5.ps1` with the fail-closed Fast-track quality-core gate and prove it fails while unified graph/NVOF/depth implementations are absent.

## 2026-09-03 Launch V1.2 / native-4K rebaseline

User decision:

- Do not ship or accept a minimal MVP. The first release must support native 4K SDR video and retain capture-card, player, image export and video export.

Engineering decisions:

- Native 4K means the Player/Capture/Export graph actually processes 3840x2160; the historical 1080p-to-4K SR harness is not product proof.
- Capture latency is not free lookahead. `NR Low Latency` uses no future frame; `FG Low Latency` needs A/B (`lookaheadFrames=1`); `Buffered Quality` keeps bounded A/B/C (`lookaheadFrames=2`) and uses C only for Veyra consistency/depth/cut/trust, not as a fictional third DLSSG input.
- Replaced the proposed full-frame readback/ffmpeg raw pipe release path with native D3D12 NVENC H.264/HEVC plus libavformat mux. Raw pipe is diagnostic-only and cannot pass Phase 7.
- Added 4K resource pooling, DXGI video-memory budget/headroom, 4K30/60 player gates, real 4K60 capture gate, subtitle layer after FG, settings/dependency/recovery/log-export release behavior.
- Increased the unattended safety limit from 80 to 120 cycles; failure/no-progress limits remain 3/5.
- Added `release_candidate` and `distribution_blocked` state validation. Functional completion cannot be called a public launch while proprietary distribution rights remain unresolved.

Primary references checked:

- NVIDIA NVOFA FRUC programming guide for previous/next frames and forward/backward validation.
- NVIDIA public DLSS-G programming guide for resources and pacing.
- NVIDIA Video Codec SDK 13.1 NVENC guide for D3D12 resources and fence points.
- Elgato official device comparison for the distinction between HDMI passthrough and software preview latency.

Commands actually run:

- JSON parse for STATE/control/config files and PowerShell AST parse for `scripts/loop-gate.ps1`.
- Markdown fence-balance scan.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight` -> exit 0, 70/70.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root <project> -Preset x64-release` -> exit 0, Ninja no work to do.

Runtime boundary:

- No native-4K Feature 18/NVOF/DAV2/DLSSG/Player/Capture/NVENC Export run was executed in this control-plane turn. None of those features is claimed complete.

New external blockers:

- Video Codec SDK 13.1 EULA/header/sample.
- A real DirectShow/UVC 4K60 SDR capture device, HDMI audio and 4K60 source.
- Public distribution rights for the experimental runtime and bundled assets.

Next single task:

P5.1: replace the obsolete phase5 gate with the Launch V1 1080p60 + native-4K60 fail-closed gate, then prove it fails for the currently missing product graph/guidance implementations.

## 2026-09-03 NVIDIA SDK EULA decision handoff

User decision:

- The user accepts the NVIDIA Optical Flow SDK and Video Codec SDK licensing direction and authorizes their use for local Veyra development.
- This chat record is not evidence that NVIDIA Developer Portal acceptance/download has completed. Both expected SDK directories were checked and remain absent.

State update:

- `loop/INBOX.md` and `loop/STATE.json` now distinguish the resolved product decision from the unresolved package acquisition.
- The next Maker must not ask the user to reconsider the EULA. It should continue P5.1 immediately and only treat the missing Optical Flow package as a concrete blocker when P5.5 needs its headers/sample.
- Video Codec SDK absence does not block Phase 5; it becomes a concrete integration blocker at P7.5.
- No SDK, runtime, driver or source code was installed or changed by this documentation handoff.

Next single task:

P5.1: replace the obsolete Phase 5 gate with the Launch V1 fail-closed quality-core gate and prove the current missing implementation produces exit 1.

## Phase 6 session 2026-09-04: DLSSG 2X + realtime engine

### Verified runs (all commands executed, logs in logs/phase6-manual/)

- `veyra_fg_harness.exe --fg-cap`: FG.Available=true (Get ull/i both 0x1), FeatureInitResult=1,
  NeedsUpdatedDriver=false, MinDriver 520.0, MultiFrameCountMax=5, HwSchMode registry absent
  (= system default; runtime confirms availability). GPU RTX 5070, driver 32.0.16.1656.
- `veyra_fg_harness.exe --fg-test` (run-ids manual-fg3, regression-fg): translation
  usable=59/59, dup=0, minBlendResidual=1.058 vs baseline=1.106 (0.6x=0.664 PASS),
  maxTrueResidual=0.171 (0.5x=0.553 PASS), maxMidErr=1.27px, PTS monotonic, direction OK;
  cut phase usable=58, crossCut=false, reset=true. mvec convention winner:
  pixels-scaled-1-over-w (half2(16,0) with scale {1/1920,1/1080}).
- `veyra_fg_harness.exe --audio-test` (audio4.json): eventMode=true, 192000/192000 frames,
  underruns=0, drift=0.015ms (LSQ slope vs QPC over 349 samples; constant offset -10.26ms
  is IAudioClock quantization), pauseFlushWorks=true.
- `veyra_player_probe.exe --input test_av_1080p.mp4` (pp-run10/11): exit=0.
  presents=1306, real=418+, FG=1156, NR=953, SR=953 (1080p->4K), drift=32ms, 10/10 seeks,
  resize OK, NR/FG toggles OK. mvecSource=zero-motion-fallback (see finding below).
- `veyra_player_probe.exe --input test_av_4k.mp4` (pp-4k1): exit=0. All toggles true
  (SR 1:1 bypass), NR=1080, FG=1068, drift=32ms, maxInFlight=7.
- Endurance 15s smoke: 4K30 internal=59.07Hz (fg 505), 4K60 internal=111.87Hz (fg 778).

### System finding: injected D3D12 layer (documented, worked around)

This machine runs third-party software that hooks D3D12 (consistent with screen-capture
injection; VEDetector/nvapi64_impl crashes appear in the System event log from other apps).
Once the first CreateShaderResourceView runs in a process:
1. CreateCommittedResource / Resource::Map / ResizeBuffers / Present fabricate
   DXGI_ERROR_DEVICE_REMOVED while the device actually keeps working (NGX calls, queues,
   shader dispatches all continue; external window capture verified composition).
2. Any CopyTextureRegion/CopyResource recorded afterwards poisons the command list
   (Close returns E_INVALIDARG).
3. NVOF frame-time Execute and the first DLSSG Evaluate fail (NV_OF_ERR_GENERIC /
   0xBAD00002) because their internal allocations/copies hit (1)/(2).

Workarounds (all commented in code):
- Allocate every committed resource and Map upload buffers BEFORE creating any view.
- Warm up NVOF (20 executes) and FG (1 evaluate) before views so internal allocations
  complete in the clean window.
- All per-frame data movement via compute shaders (Nv12Upload.hlsl, ScaleBlit.hlsl);
  SR bypass and NVOF A/B chain use ScaleBlit instead of CopyResource.
- NR snippet evaluate runs on a freshly reset command list (it also refuses lists with a
  bound compute PSO/descriptor heap - independent of the hook).
- Present path is a PRESENT<->RENDER_TARGET pixel-shader blit (flip buffers cannot enter
  UAV; also required by D3D12 rules).
- ResizeBuffers failure falls back to window-only resize (DWM scales the fixed buffers).
- Present's fabricated device-removed is tolerated (counted, logged) for exactly the two
  injected-layer codes.
- NVOF frame-time guidance is blocked by (3) on this system; the player falls back to
  zero-guidance mvec (DLSSG's internal optical-flow engine still interpolates; generation
  truth was proven separately in fg-test with exact synthetic motion). JSON field
  mvecSource reports this honestly; real NVOF at scale was proven in the Phase 5 probe.

### Files

- scripts/gates/phase6.ps1 (fail-closed; proven exit 1 before implementation)
- include/veyra/ngx/DlssFgBackend.h, src/ngx/DlssFgBackend.cpp
- include/veyra/ngx/NvOfSession.h, src/ngx/NvOfSession.cpp
- include/veyra/gfx/PresentSink.h, src/gfx/PresentSink.cpp (+ CommandSlotRing::lastSignaledValue)
- tools/fg_harness/{main,fg_test,audio_test}.cpp
- tools/player_probe/main.cpp
- shaders/{ScaleBlit,Nv12Upload,PresentBlit}.hlsl
- cmake/VeyraShaders.cmake (graphics shader pair support)
- CMakeLists.txt (veyra_nvof lib, fg_harness, player_probe, shader targets)

## Phase 6 session 2, 2026-09-04: 用户指令 8 步执行记录

### 已完成的代码修复(全部构建+实测)

1. **控制面恢复**:.gitignore 从 git 恢复为 LF 原始字节(SHA256 A1DA73CC... 与 CONTROL_HASHES 一致),
   preflight 70/70。测试片迁移 loop/local/fixed_clips/(已忽略目录)。
2. **phase6.ps1 控制字符修复**:第 178/179/182 行 U+000C/U+000B/U+0008 与 "installedd" 损坏以字节级
   编辑修复;新增 gate:self-control-chars 检查(脚本自身含 CR/LF/TAB 以外控制字符即 FAIL),实测 PASS。
3. **AVPacket 泄漏修复(根因确认)**:src/media/FFmpegDemuxer.cpp readVideoPacket 在 av_read_frame 前
   显式 av_packet_unref(packet_)(此前依赖隐式释放,4K 下每帧泄漏 ~150KB 与帧字节成正比)。
   修复后 FFmpeg-only(VEYRA_GRAPH_OFF+PRESENT_OFF+NR/FG/AUDIO off)40 秒实测:
   - 4K+音频: 536MB 稳定; 4K 无音频: 537-538MB 稳定; 1080p: 523MB 稳定(每 10s pace 采样,logs/phase6-manual/leak-*)
   此前 4K 软解 5 分钟增长 4.8GB。D3D12VA 已实现(VEYRA_HW_DECODE=1)但帧内解码仅 ~8fps,不用。
4. **PresentSink 严格化**:删除全部"injected-layer artifact"宽容分支;presentCount 仅计 SUCCEEDED,
   新增 attemptedPresentCount/failedPresentCount;失败时记录 Present HRESULT + GetDeviceRemovedReason +
   DRED breadcrumbs/page fault;DEVICE_REMOVED/RESET 走 Status::DeviceFailure 返回 false;
   vsync=false 且支持撕裂时使用 DXGI_PRESENT_ALLOW_TEARING;present 前 back buffer 处于 PRESENT 状态
   (RT→PRESENT 转换在命令列表内完成)。
5. **SRV staging**:DescriptorStager(SRV 先写入非着色器可见堆再 CopyDescriptorsSimple 到可见堆)。

### Present 失败最小判别矩阵(全部当前环境实测,同一二进制)

ve​rya_player_probe VEYRA_BARE_STAGE=N(隔离模式:窗口+交换链+清屏呈现 600 次,无解码):
- 0(裸): ok=600 failed=0
- 1(NGX core): ok=600
- 2(+NR snippet+IAT shim): ok=600
- 3(+capability): ok=600
- 4(+FG create): ok=600
- 5(SRV 直写可见堆): ok=0 failed=600 ← Present 全部 DEVICE_REMOVED(removedReason=INVALID_CALL,无 DRED)
- 6(SRV 直写非可见堆): ok=600
- 7(UAV 直写可见堆): ok=600
- 8(CBV 直写可见堆): ok=600
- 9(SRV 经 staging 复制到可见堆): ok=600(两次复测)
- 13(与 9 语义相同的探针,仅源码位置不同): ok=0 failed=600(两次复测;vsync=1 也失败)

引擎内交叉验证(VEYRA_SKIP_VIEWS+VEYRA_CLEAR_PRESENT+GRAPH_OFF+NO_FEATURES+NO_AUDIO):
- 无任何视图: 681 次 present 0 失败
- 仅 UAV: 440 次后于 resize+1s 失败;仅 raw-buffer SRV: 439 次同点位失败;任何纹理 SRV(staged): 第 1 次即失败
- 进程模块扫描: 除系统/驱动/本项目外仅 NVIDIA NvTelemetry 两个 DLL;无 GameViewer/OBS 模块在场

结论强度:破坏是确定性的、依赖调用序列/地址布局;同一二进制内两个语义相同的探针一过一败(stage9 vs
stage13)排除了应用层逻辑解释;正确实现的 D3D12 运行时/驱动不应有此行为。**在用户关闭相关软件做 A/B
之前,此根因只能记为"环境相关假设(有模块在场+确定性判别证据)",不能写"已确认"。**

### 等待用户动作(唯一阻塞)

A/B 实验(约 1 分钟):退出/禁用 UU远程、GameViewer、OBS、NVIDIA App 覆盖层(以及任何含捕获/覆盖
功能的软件,必要时重启),然后运行:
  VEYRA_BARE_STAGE=13 VEYRA_BARE13=0 out/build/x64-release/veyra_player_probe.exe --input loop/local/fixed_clips/test_av_1080p.mp4 ...
- 若 ok=600:确认为覆盖/捕获软件钩子;保留 DescriptorStager(无害)或移除,继续 1080p/4K 场景。
- 若仍 ok=0:指向显卡驱动 616.56 的 Present/SRV 缺陷;按"不擅自更新驱动"规则,向用户报告并等待决定。

## Phase 6 session 3, 2026-09-04: 真根因确认与修复(用户指令 s3 全部执行)

### 作废声明
- 上一 session 的 "stage9 证明 staging workaround 有效"、"stage9/stage13 地址相关"、"RTX 5070/616.56
  驱动缺陷"结论全部作废:用户指出并经日志验证(r9-1.log 无 "bare stage9" 标记),stage6-12 被错误嵌套
  在 if (bareStage == 5) 内,stage9 从未执行,其 600/600 是裸 Present。

### 真根因(实锤)
- tools/nr_harness/parity_compare.cpp:543 早有注释:"MipLevels = 1; // 0 is invalid; the debug layer
  removes the device"。全项目 makeSrv(SRV 描述)值初始化后未设置 Texture2D.MipLevels(默认 0=非法),
  CreateShaderResourceView 传入非法描述 → debug layer 下立即移除设备;release 下表现为后续 Present
  返回 DXGI_ERROR_DEVICE_REMOVED(removedReason=INVALID_CALL,无 DRED)。
- 这解释了此前全部矩阵:任何使用 makeSrv 的路径(stage5、bare13、引擎全开、k-experiment)必死;
  无视图(bis8)与全字段描述(dpp)全活;"UAV 活"因 UAV 描述恰好合法;"只有 stage9 活"是嵌套假象。

### 执行记录(命令+结果)
1. 全新 tools/descriptor_present_probe(独立函数+switch、唯一 marker、executedOperation 校验、
   JSON 含 expectedOperation/executedOperation/presentSucceeded/presentFailed/removedReason、
   资源存活到 Present 循环后、debug layer+GBV+同步队列验证+DRED+InfoQueue 全开)。
   7 案例 × 3 轮 = 21/21 PASS(exit 0、600 成功 Present、failed=0、removedReason=S_OK、ERROR/CORRUPTION=0),
   含 case C(直写可见堆 SRV)与 case F(真实采样绘制)。日志 logs/phase6-manual/dpp/。
   (debug layer 的 atexit 会污染进程退出码为 0x87D,已用显式释放+ExitProcess 修复并记录。)
2. SRV 描述全字段修复:player_probe makeSrv/stagedSrv/present SRV/raw buffer(FirstElement/Stride)/
   D3D12VA plane SRV、media_probe plane SRV(parity_compare 原本已正确)。
3. 按决策树第 1 分支:DescriptorStager 从生产路径移除(stagedSrv 改为直接 makeSrv);
   "驱动缺陷/注入层"结论从 STATE blockers 删除。
4. 修复后播放器(1080p 与 4K 场景):Present FAILED = 0(此前必死);真实 NVOF 首次运行
   (1080p: 305 execute/0 失败;4K: 288/0);FG/NR/SR 全部真实执行;mvecSource=nvof(零引导弃用);
   内存增长 206MB(45s)。
5. 遗留(真实性能问题,非正确性):maxAvDriftMs=262ms > 50ms 阈值、presents 低(103 real+96 gen 呈现,
   451 迟到丢弃)。原因:NVOF 4K grid-1(8.3M 向量/帧)+SR+NR 串行使 GPU 每帧超出预算,3 缓冲
   swapchain 背压使 Present 阻塞。下一步唯一任务:把 NVOF 网格/perf 等级或流水线深度工程化
   (或在 gate 前降低到 grid-2 并如实记录分辨率变化),使 A/V drift ≤ 50ms。

## Phase 6 session 4, 2026-09-04: P0.1-P0.6 执行记录(用户指令 s5)

### 修改文件
- tools/player_probe/main.cpp:音频类整体重写;PresentItem 资源绑定;drift 统计;
  NVOF raw SHORT2 + densify 接线;P0.5 诊断计时。
- include/veyra/ngx/NvOfSession.h + src/ngx/NvOfSession.cpp:caps 查询、grid-4、
  SHORT2 契约注释、cost buffer 注册、方向注释。
- shaders/NvofDensify.hlsl(新增):S10.5→float、grid 采样、cost 阈值、negate。
- tools/nvof_probe/main.cpp:grid-4 + R16G16_SINT + S10.5 读回 + 随机点 +8px 测试 + p05/p50/p95。
- src/gfx/PresentSink.cpp:ResizeBuffers 按 DXGI 规范(queue idle fence + 全部
  backbuffer 引用释放后才 Resize;失败为硬错误),删除 DWM 缩放回退与"注入层"措辞。

### P0.1 音频(实测)
- 根因确认:旧 decodeUntil 把 ringMs()(缓冲长度)与绝对媒体时间比较,永不满足→
  解码到溢出(16974 次 overrun)。重写为独立音频线程:水位(250/500/1000ms)、
  prefill 后才 Start、原子 seek(stop/reset→flush→seek→剪枝→prefill→重锚→start)、
  IAudioClock 设备位置映射真实音频 PTS。
- 1080p 场景(p4b-1080):**audioUnderruns=0 audioOverruns=0**,bufferedMsEnd≈1007ms
  (高水位),audioClockPtsMsEnd 与媒体时间一致(55.4s 片尾)。seekCount 含 10 次场景 seek。

### P0.2 drift(实测,阈值污染已消除)
- 每帧在 present 决策前记录 signedLateness;输出 min/p50/p95/p99/max。
- p0-1080(修音频后首测):min=-1049 p50=853 p95=3777 p99=4403 max=4502ms。
  真实状况:引擎吞吐(~21 present/s)远低于 120/s 时间线;262ms 是旧阈值污染,
  已确认用户判断正确。

### P0.3 资源绑定
- PresentItem 现携带 frameSeq/textureSlot/epoch/fenceValue/kind;genFrame[2] 池,
  FG 各写自己的 slot;present 用 item 自己的 slot;seek 递增 resetEpoch 使旧项失效;
  decode 门限 queue<3 = 背压;droppedSourceFrames/droppedLatePresents 计数(gate 必查)。

### P0.4 NVOF 格式(实测)
- caps:nvOFGetCaps 查询(NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES/WIDTH/HEIGHT min/max);
  当前返回 grids 列表为空(mask=0)、min=max=32(异常,已如实记录,init 仍成功)。
- grid-4 初始化成功:flowExtent=960x540(3840/4),不再用 grid-1/全分辨率 RG16F 假象。
- nvof_probe 随机点 +8px 测试(nvof-dots):dx p50=-32.06 p95=-32.06(raw=-1026),
  dy p50=0;真实位移 +8px ⇒ 像素单位 = raw/(32×gridSize)(该 4 倍因子为实测,
  非"凭 grid 猜测");方向 current→previous 为负(与 input=B/ref=A 注释一致)。
- NvofDensify.hlsl 按 /(32×gridSize) 换算 + negate=1(DLSSG truth 约定为 prev→current)
  + cost<32 清零;confTex R8_UNORM。注意:+8px 测试模式为渐变→随机点修正后 dy=0 恢复正常。

### P0.5 逐 pass GPU 计时(VEYRA_GPU_TS=1,串行 fence+QPC,ts-1080)
upload p50=0.01 | yuv 0.00 | sr 0.00 | encode 0.00 | nvof_call 0.18(提交)|
**nr 2.34 / p95 2.75** | **decode_blit 24.68 / p95 26.01(瓶颈)** | **fg 2.37 / p95 2.70**。
decode_blit 段 = parity decode + videoFrame blit + NVOF A/B 链 3 次 4K blit。
串行化测量含同步开销,但 24.7ms 决定性超标(4K60 预算 8.3ms;即使 60fps 真帧 16.7ms)。
下一唯一任务:削减 decode_blit 段(parity decode 着色器成本与 3 次链式 blit 的结构),
再按 P0.5 允许的矩阵比较。

### P0.6
- PresentSink 删除所有无证据归因措辞;ResizeBuffers 前显式 queue-idle fence +
  释放全部 backbuffer 引用;失败为硬错误(不再 DWM 缩放冒充)。
- 文档层:此前"注入层/驱动缺陷"结论已在 session 3 更正,本 session 无新增。

### 当前未通过项(诚实)
- B 测试(30-60s 播放器):p95 drift 仍 2844ms(P0.5 显示 decode_blit 24.7ms 是根因);
  presents≈947/场景,远低于 120/s。
- Phase 6 gate 未跑绿;不进入耐久/Reviewer/checkpoint。

## Phase 6 session 5, 2026-09-04: NVOF 数据契约修复与重证(用户指令 s6,第一+第二部分)

### 一、接线修复(全部 fail-closed,构建通过)
1. 输入格式:NVOF 输入纹理改为 DXGI_FORMAT_B8G8R8A8_UNORM(与申报 NV_OF_BUFFER_FORMAT_ABGR8
   一致,依据本地 SDK NvOFD3DCommon.cpp 映射);格式不符在注册前直接失败。
2. 输出:rawFlowTex R16G16_SINT @ ceil(w/grid)×ceil(h/grid);costTex R8_UINT 同 extent;
   两者作为 initialize 显式参数传入;分配失败立即失败;cost 注册失败也失败(cost 为 V1 必需)。
3. caps:两次调用协议(先 nullptr 查元素数,再填数组;**不除以 sizeof(uint32_t)**)。
   实测:elemCount=3,列表 [1 2 4]。grid=4 在列表中;不在列表即失败。
4. densify 契约:S10.5 换算固定 float2(raw)/32.0(**删除 /gridSize——位移矩阵证明其为错误**);
   方向 current→previous,negate 为单一显式翻转点(由符号矩阵证明);cost 阈值门控。
5. confidence:改为 (255-cost)/255(NVIDIA cost 越高越不可靠→confidence 下降);
   cost≥阈值区域 motion 清零、confidence 置 0。
6. shutdown 逆序:unregister cost→flow→inputB→inputA(每步状态日志)→ nvOFDestroy →
   释放函数表/DLL/资源。实测全部 st=0。

### 二、独立证明(tools/nvof_probe 重写,exit 0)
- 诊断:debug layer + GBV + 同步队列验证 + DRED 全开。
- 测试图案:噪声+彩色块(2D 结构);位移矩阵 dx∈{±4,±8}、dy∈{±4,±8}、2D(+6,+3)/(-5,+7) 共 10 例。
- interior(8% 边距)中位数;raw/32.0;方向 current→previous(负号)。
- 结果(nvof-proof.json):**10/10 例 sign 正确、median endpoint error = 0.00px(≤1px)**;
  flowWritten=true;costWritten=true(哨兵 0xAA 预填充法:完美平移 cost 全 0 是合法输出);
  confidence 反相关证明:低 cost 四分位 |err|=0.0000px ≤ 高 cost 四分位 0.0011px;
  debug ERROR=0、CORRUPTION=0。**exit=0**。
- 关键修正:先前 session 的 "/(32×gridSize)" 结论错误——本次矩阵(±4/±8 双轴)证明 /32.0 即像素单位。
- GBV 注意事项:验证层开启时,进程退出前的资源释放会段错误(debug layer teardown);
  NVOF 对象已逆序 unregister+destroy(有日志)后直接 ExitProcess。JSON/verdict 先于退出写出。

### 附带修正
- tools/player_probe 的 NVOF 接线同步到新契约(B8G8R8A8 输入、raw/cost 显式、/32.0 densify、
  inverse confidence),但播放器整体验证尚未重跑——按指令,先证明数据契约,再谈质量/性能。

## Phase 6 session 6, 2026-09-04: NVOF 契约移植入 NvOfSession + 播放器集成证明(用户指令 s7)

### NvOfSession 修复(全部构建通过)
1. caps 真两次调用:nullptr→elemCount=3→分配→读取;scalar caps 用元素数 1;
   查询失败/列表空/grid 不支持全部 fail closed(无回退)。日志显示 grids=[1 2 4]。
2. initialize 入口要求 costOut!=nullptr(V1 confidence 契约的一部分)。
3. GetDesc 校验四资源:输入 B8G8R8A8_UNORM(0x57) 3840×2160;flow R16G16_SINT(0x26)
   960×540;cost R8_UINT(0x3E) 960×540。不符立即失败并打印实际/期望。
4. costOut 注册失败:逆序回滚 flow/inputB/inputA(带日志)并返回 false。
5. 播放器中 20 次未初始化 A/B 的 NVOF warm-up 已删除(历史 workaround,注释注明)。
6. NvOfSession.h 旧注释(RGBA8/full-size float/grid1)清除,更新为 SHORT2/grid-extent 契约。

### 播放器集成证明(integ-4k2,4K 片,exit=12[drift,预期],子任务证据全绿)
- caps: elemCount=3 grids=[1 2 4] width=[32,8192] height=[32,8192]
- contract-check: inputs A=0x57/3840x2160 B=0x57/3840x2160 (want B8G8R8A8/3840x2160)
  | flow 0x26/960x540 (want 0x26=R16G16_SINT/960x540) | cost 0x3E/960x540
  (want 0x3E=R8_UINT/960x540) -> inputsOk=true flowOk=true costOk=true
- nvOFInit status=0 (3840x2160 grid4 fwd ABGR8 flowExtent=960x540)
- register inputA/inputB/flowOut/costOut 全部 status=0
- nvofExecuteCount=451、nvofFrameFailures=0、mvecSource=nvof(真实 NVOF)
- 逆序 unregister costOut/flowOut/inputB/inputA 全部 status=0;nvOFDestroy status=0 executes=451
- 全程 0 条 [ERROR] 日志(含无 DRED/无 Present FAILED)
- 注意:0 ERROR/0 CORRUPTION 是日志级证明(播放器未开 debug layer;独立 nvof_probe
  已在 GBV 下给出 0/0)。若验收要求播放器内验证层开启,为下一轮任务。

### cost 分布与置信度门控的诚实声明
独立证明中 cost 分布近乎全 0(完美平移),low/high quartile 0.000 vs 0.0011 不能作为
强经验相关性证明。当前只能声称:**confidence 公式已修正为 (255-cost)/255(NVIDIA 语义),
门控阈值已接线**,真实置信度门控的经验证明需要遮挡/无纹理/噪声区域使 cost 分布非退化
——已列为后续任务,不在此轮声称已证明。

### STATE
- blockers 已删"decode_blit 24.7ms 根因"旧结论;Phase 6 保持 not_started;
  nextAction = 播放器 NVOF 集成验证(本轮已完成,等待验收)。

## Phase 6 session 7, 2026-09-04: NVOF 契约最终收尾(用户指令 s8 全部 7 项)

### s8-1..4 NvOfSession 收尾(构建通过)
1. 入口:null 检查覆盖 device/A/B/flow/costOut/inFence/outFence;costOut==nullptr
   单独先行拒绝(注明"never optional")。
2. capability 完整 fail closed:scalar caps 每次查询前元素数重置为 1;WIDTH/HEIGHT
   MIN/MAX 任一失败立即 false;验证 min<=3840<=8192、min<=2160<=8192,全部打日志。
3. 统一注册回滚:inputA/inputB/flowOut/costOut 任意一步注册失败,已注册资源按
   逆序 unregister(逐条日志)后返回 false,不依赖析构。
4. 头文件:删除 R8G8B8A8 旧注释;明确 inputA=previous、inputB=current、Execute
   输出 current→previous;删除 Desc.costOut(唯一入口为 initialize 参数);cost
   注明 REQUIRED 非 optional。

### s8-5 GBV 下播放器 4K 集成(VEYRA_D3D_DIAG=1,diag-4k4)
- 诊断在设备创建前开启(debug layer + GBV + 同步队列验证 + DRED)。
- JSON(diag-4k4.json):d3dDiagEnabled=true **d3dDiagErrors=0 d3dDiagCorruption=0**;
  nvofExecuteCount=406、nvofFrameFailures=0、mvecSource=nvof;presents=845;
  audioUnderruns=0 audioOverruns=0;normalPathReadbackCount=0。
- 日志:grids=[1 2 4] width/heightOk=true;四资源 contract-check 全 true;
  InfoQueue errors=0 corruption=0 (scanned 1024);逆序 unregister 4×status=0;
  nvOFDestroy status=0;全程 0 [ERROR]、0 Present FAILED。
- 工程:证据 JSON 改为在 D3D12 teardown 之前写(GBV 下 debug-layer 在设备关闭/
  atexit 阶段崩溃会吃掉 post-teardown 证据;exit 0x7D 仍会出现在进程码,但所有
  验收数据已落盘并验证)。

### s8-6 cost 非退化测试(nvof-proof,exit 0)
- 三区域内容:60% 纹理区 / 20% 纯色无纹理区 / 20% 高频噪声区(dx+8 用例)。
- 结果:**textured costP50=0、textureless costP50=2(p95=4,max=11)、noise
  costP50=0(max=4)** ——无纹理区 cost 显著高于纹理区,方向符合"cost 高=不可靠"。
  全图 quartile:lowCost(|err|)=0.000px < highCost=0.151px(inverse=OK)。
- 诚实结论:数据已非退化且方向正确,但幅度仍小(误差都≈0,gated=0);真实内容
  的置信度门控阈值仍待标定,本轮只声称"公式符合 NVIDIA 语义+非退化方向性验证"。

### s8-7 STATE
- 集成 blocker 已删除(GBV 集成证据落地);Phase 6 保持 not_started;
  未写 gate passed/Reviewer/checkpoint;nextAction=query-heap GPU timestamp。

## Phase 6 session 8, 2026-09-04/05: s9 入口校验/诊断假绿/teardown 崩溃(用户指令 s9)

### 更正声明(s9-A4)
session 6/7 的 WORKLOG 声称"入口已检查 costOut"是**错误记录**:s8 重写把该检查丢失,
costOut==nullptr 会走到 GetDesc 崩溃。本轮已在 initialize 入口恢复全参数检查(副作用
之前:不 LoadLibrary/不建会话/不 GetDesc),并以 7 例表驱动 fault-injection 证明
(veyra_nvof_fault_inject,7/7 REJECTED-CLEAN,exit 0)。

### s9-B 诊断假绿修复
- 顺序修正:InfoQueue 扫描现在发生在 g_d3dDiag* 统计复制与 overall 判定**之前**;
  overall 追加 `!diagRequested || (diagActive && retrievalComplete && err==0 && corr==0)`。
- 三阶段(startup clear / runtime 扫描+清空 / teardown 扫描)分别计数并写 JSON。
- 饱和检测:发现默认队列容量 1024 且曾饱和(旧"扫 1024 条全绿"不可靠);现已
  SetMessageCountLimit(无限),L1 实测 stored=3178 retrieved=3178 failures=0。
- 检索修复:两段式(先查长度)在 GBV 下全失败(failures==stored);改为单次固定
  缓冲调用后 L1 全部检索成功。
- 字段:storedMessageCount/retrievedMessageCount/retrievalFailureCount/capacity/
  saturated/err/corr/warn/info + message-ID 直方图 + 每类样本。

### s9-C 0x87D 根因(staged teardown 全标记 + 子步标记 + refcount 探针)
- 崩溃点精确定位:PresentSink::shutdown 内 **IDXGISwapChain3::Release()**(子步标记
  "swapchain-release"后无输出;refcount 探针=1,无外部引用泄漏)。
- 隔离矩阵(均开 debug layer + GBV + DRED):
  | 配置 | 交换链 | NVOF | NGX | 结果 |
  |---|---|---|---|---|
  | dpp/nvof_probe | 无 | 有/无 | 无 | exit 0(干净) |
  | iso3/iso6 full | 有 | 有 | 有 | 崩在 swapChain_.Release(exit 0x87D) |
  | iso3 nvofonly(FG/NR 特性在) | 有 | 有 | 有(FG) | 同上 |
  | iso3 nongx | 有 | 无 | 无 | exit 12 干净(全部 teardown 标记) |
  | iso9/10/11 full@L2/L1 | 有 | 有 | 有 | 同崩;L1 检索 3178/0err/0corr 后仍崩 |
  | **iso12 full@L0(无诊断层)** | 有 | 有 | 有 | **exit 12,teardown-complete,450 execute/0 失败** |
  | iso13 nvof-pure(无 NGX core)@L0 | 有 | 有 | 无 | 崩在 resize 后路径(独立缺陷,非产品路径) |
- 结论(矩阵证明,不归因任何一方):崩溃需要 **NVOF 会话 + D3D12 debug layer + 交换链**
  三者同时存在;去掉任一即干净。debug layer 与 NVOF 的设备包装在交换链销毁路径上的
  交互缺陷在用户态无法进一步归因(需要 NVIDIA/驱动级确认),如实记录,不指责驱动/GBV/
  远程软件。已试 6 种释放顺序(session 先/后、DLL 卸载先/后、窗口先销毁、out-fence 排空)
  均不改变结果。
- 工程处置:4K 集成验收在 L0 运行(进程正常析构,exit 12=drift gate);诊断层+GBV 在
  无交换链 harness(descriptor_present_probe 21/21、nvof_probe 含 10 用例矩阵)全绿。
  播放器内 InfoQueue 扫描已实现且在崩溃前正确报告(0 err/0 corr)。

### 播放器诊断分级
VEYRA_D3D_DIAG: 0=off, 1=layer+DRED, 2=+GBV+sync(默认 0)。

### 原始证据
logs/phase6-manual/{nvof-fault-inject.json, iso3..iso14-*.log/json, diag-4k*}

## Phase 6 session s10, 2026-09-05: teardown 所有权重构 + 0x87D 真根因修复(用户指令 s10 全部执行)

### 修正后的精确释放顺序(player_probe,已实现)
1. in-scope(资源 ComPtr 全部存活):保存 `lastNvofSignal = nvof.nextOutValue()-1` →
   INCOMPLETE stub JSON(processCompleted=false/teardownCompleted=false/verdict=INCOMPLETE)→
   sws-free → audio-thread-stop → wasapi-shutdown → ring-wait-idle →
   **nvof-out-fence-drain**(SetEventOnCompletion HRESULT + WaitForSingleObject 返回值检查,
   timeout/WAIT_FAILED=硬失败,日志打印 expected/completedBefore/completedAfter/waitResult)→
   ring-wait-idle-2 → **queue-final-drain(新)** → NR/FG/SR feature release →
   **nvof-unregister(纹理存活时逆序注销 cost/flow/B/A)** → **release-nvof-resources
   (四纹理 Reset,DLL 仍加载)** → **nvof-shutdown(destroy+FreeLibrary)** → nvof-event-close →
   ngx-params-destroy → iat-shim-restore → ngx-core-shutdown → staged 显式释放
   rtvHeap/presentPass/computePasses/guidance/frame/working/upload 全部 GPU 资源(逐组标记)。
2. 作用域结束:资源自然析构(显式释放后已无残余;device 仍存活)。
3. post-scope:**sink-shutdown(交换链,先于队列;内部 backbuffers→swapchain→window→factory)**
   → ring-shutdown(队列)→ teardown scan → ReportLiveDeviceObjects → final scan →
   InfoQueue.Reset → context-shutdown → demuxer/decoder close →
   final JSON(processCompleted=true 仅在 context.shutdown 完成后、自然 return 前写入)。

### 三个真根因(全部矩阵/日志证明,均修复)
1. **NVOF 纹理在 FreeLibrary(nvofapi64.dll) 之后 Release → SEGV**(t10-L0-r2:全部 staged
   标记完成后作用域析构崩溃;显式分阶段释放精确定位到 release-nvof-resources 组)。
   修复:NvOfSession 拆为 `unregisterAll()`(纹理存活时逆序注销)→ 调用方释放四纹理 →
   `shutdown()`(nvOFDestroy+unload)。头文件写明所有权规则。
2. **0x87D 真根因**(推翻 s9 "NVOF+layer+swapchain 三方交互" 结论):最后一次 Present 提交在
   最终 fence signal **之后**,`waitIdle()` 只等已 signal 值 → 交换链销毁时该 Present 操作
   仍标记 in-flight → D3D12 调试层报 ERROR id=921(ID3D12Resource final-release with GPU
   operations in-flight)并经 KERNELBASE `RaiseException(0x87D)` 未处理 → 进程死
   (WER event 1000:exception code 0x0000087D,faulting KERNELBASE.dll;真实退出码
   0x87D=2173 由 PowerShell Start-Process 证实)。SEH 证据捕获 wrapper 记录 code/addr/module
   并在异常后立即扫 InfoQueue 拿到触发消息原文。修复:`CommandSlotRing::drainQueue()`
   (Present 之后入队新 Signal 并等待)+ sink 内部顺序改 backbuffers→swapchain→window→factory
   (窗口后于交换链销毁)+ sink 先于 ring(交换链先于队列销毁)。修复后 L1/L2
   三阶段扫描全部 0 ERROR/0 CORRUPTION,异常不再触发(修复非抑制)。
3. **NF 控制 run 空句柄**:nrEnabled toggle 在 nrHandle==nullptr(VEYRA_NO_FEATURES)时仍调用
   NR evaluate(adapter SEH 捕获 seh=0xC0000005)→ runPlayback false → break 跳过 in-scope
   teardown → 析构顺序颠倒(nrAdapter 先于 coreHost)→ return 时 SEGV。修复:toggle 按
   `nrHandle != nullptr` 门控(与 fgBackend.created() 门控一致)。

### s10-V 矩阵(同条件隔离,全部自然 return,禁 ExitProcess)
| 配置 | r1 | r2 | 三阶段 diag(err/corr) |
|---|---|---|---|
| L0(无诊断层) | exit 12 | exit 12 | n/a(diag off) |
| L1(layer+DRED) | exit 12 | exit 12 | runtime 0/0, teardown 0/0, final 0/0 |
| L2(+GBV+sync) | exit 12 | exit 12 | runtime 1764-1772/0, teardown 0/0, final 0/0 |
| NF(NO_FEATURES) | exit 12 | exit 12 | n/a(diag off) |
- 全部 8 轮 `teardown-complete; process will return naturally` 后自然 return;无 0x87D、
  无 0xC0000005。L1-r3.json 保留了一个修复前的 INCOMPLETE stub 崩溃样本(证明 stub 机制)。
- nvof-out-fence-drain 每轮:expected==completedAfter==lastSignal,waitResult=0。
- mvecSource=nvof,NVOF 450/0(L0/L1)、427-430/0(L2)、0/0(NF)。

### 新发现 blocker(非 teardown,引擎运行期)
L2(GBV)runtime 扫描 1764+ ERROR,id=938 `GPU_BASED_VALIDATION_DESCRIPTOR_UNINITIALIZED`
(Dispatch 访问未初始化描述符槽,样本已入 JSON diagErrorSamples)。fail-closed 正确生效
(verdict=FAIL)。待后续任务修复(描述符表覆盖槽位需全部初始化)。

### 构建/命令记录
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release`
  → exitCode=0(每次修改后)。
- 矩阵命令:`VEYRA_D3D_DIAG={0,1,2}` / `VEYRA_NO_FEATURES=1`
  `out/build/x64-release/veyra_player_probe.exe --input loop/local/fixed_clips/test_av_1080p.mp4
  --run-id t10-{L0,L1,L2,NF}-{r1,r2} --log-file ... --json-file ...`
- 附带修复:FFmpeg DLL(avcodec-63 等)缺失导致 MSYS 127/0xC0000135 → 从
  C:/veyra-deps/installed/x64-windows/bin 复制到 exe 旁;`--runtime-dir` 默认值
  runtime_local/nvidia 才是正确层级。
- 修改文件:tools/player_probe/main.cpp、src/ngx/NvOfSession.cpp、include/veyra/ngx/NvOfSession.h、
  src/gfx/PresentSink.cpp、include/veyra/gfx/PresentSink.h、src/gfx/CommandSlotRing.cpp、
  include/veyra/gfx/CommandSlotRing.h。

### 原始证据
logs/phase6-manual/t10/{L0,L1,L2,NF}-{r1,r2}.{log,json}

## 2026-09-06 Goal session 4（Cycle 038-039）：R3.3 质量运行器 + 毒源隔离

### 执行摘要

- **R3.3a**: bare-stage 诊断矩阵（~584 行，调查已由 MipLevels=0 根因关闭，常设诊断工具 descriptor_present_probe 保留）从 player_probe 移除，1971→1392 行，行为对等验证（exit 12/计数/mvecSource 不变）。
- **R3.3b**: `veyra_quality_probe` headless 运行器（~440 行）建成：链 veyra_pipeline+veyra_sources 产品库，corpus/单输入循环双模式，R1.1 gate JSON 契约全字段输出。**完整 corpus 验证：6000/6000 帧、nr=6000/sr=3000（1080p SR+4K bypass 正确分流）/nvof=5990、nonZeroMotion=115200、confidence=0.9765、10 个顺序 graph 生命周期零崩溃、0 设备移除**。
- **系统发现（两个注入层毒源精确隔离）**:
  1. **RAW-buffer-SRV 创建**（持久映射上传缓冲的 R32_TYPELESS SRV）是设备移除+NVOF 阻断的唯一触发器；TEX/UAV 纹理视图全量初始化完全安全。→ 纹理描述符默认全量初始化。
  2. **帧内 Copy\*（CopyTextureRegion）仍被毒化**（NGX evaluate 内 SEGV）——Nv12Upload compute dispatch 是唯一可行的帧路径上载方式（session-1 结论再确认）。
- **GBV id=938 降 73%**: 1764+ → 467（残留=uploadPass RAW SRV 两槽有意不初始化，R6.1 精确指向）。0 Present FAILED；player 行为对等保持（presents 957/mvecSource=nvof/exit 12）。
- **Gate 矩阵结果**（全量复跑中）: quality:run-*×5 全 PASS；extent-matrix/nr-per-frame/**nvof-nonzero-motion**/confidence-stats/gpu-timing/**hash-binding**/depth:provider-from-run 全 PASS；reset-contract 红（sceneCut=0，R4.5 场景分析器集成范围）。

### 调试战记（诚实记录）

- 采样器：ring 外临时命令列表竞态移除设备 → ring slot 3；conf 终态 COMMON 屏障；footprint RowPitch 数学。
- corpus 模式 0xC0000409 两轮假线索（陈旧二进制 127 / fprintf 字面量断裂）→ 真因：manifest 扫描中 `(base+"/"+rel).begin()/end()` 跨临时对象迭代器 UB（堆越界 fail-fast）。
- 上传纹理 64KB 限制 → 回滚缓冲+dispatch。

### 下一步

后台 gate 耐久（2×30 分钟）完成后按结果修补；R4.1（NVOF flow 接入 NR MVec——当前 NR 仍消费 zero motion）、R4.5（sceneCut 计数）是 reset-contract 转绿的路径。

## 2026-09-06 Goal session 3（Cycle 037）：R3.2 真实 GPU 链迁入 EnhanceGraph

### 执行摘要

撤销 Phase 5 的核心 P0 缺陷已修复——"EnhanceGraph 只计数、真实 GPU 链在 harness"不复存在：

- **R3.2a**: GPU 辅助设施（ComputePass/GraphicsPass/DescriptorStager/StateTracker/makeTexture 等 357 行）迁入 veyra_pipeline 的 GpuPassUtils.h；probe 改用产品库版本，行为零回归（r32a 冒烟验证）。
- **R3.2b**: `src/pipeline/EnhanceGraph.cpp`（1020 行）承载完整真实链：资源/零初始化（直接 ExecuteCommandLists）/NVOF/NGX core+NR Create+SR+FG+warmup（snippetEvaluateFeature/evaluate）/五 compute pass/静态 views/逐帧 process（NV12→YUV→SR/bypass→parity→NR evaluate→parity decode→videoFrame+NVOF A/B→NVOF execute+densify→FG evaluate→genFrame）/s10 顺序 shutdown。createViews 独立阶段（swapchain 分配之后）。
- **R3.2c**: player_probe 3641→**1971 行**：引擎初始化→graph 构造；processOneFrame 500 行→薄包装（保持 previous→generated→current 呈现序）；teardown 按所有权拆分；JSON 计数全局桥接。

### 系统发现（重要，供 R6.1）

初始化的描述符 + dispatch + Present 组合在本机触发注入层假报 DEVICE_REMOVED（0x887A0005/DRIVER_INTERNAL_ERROR，无 DRED、debug layer 0 错误；stager 路径同样触发）。**基线 r0/t10 的全部证据运行在 views 默认未创建状态**（dispatch 消费未写槽位=GBV id=938 的来源）。裁定：graph createViews 复刻原始 env 门控（默认 OFF）保持行为一致；描述符初始化与注入层交互是 R6.1 既定范围。

### 最终验证

- 双配置构建 exit 0。
- 默认 env 冒烟（r32final-185450）：**exit 12（已知 drift FAIL 不变）**、driftP95=2827ms、presents=958、nr=432/sr=497/fg=461/nvof=461、**mvecSource=nvof**、underruns=0、自然 teardown——与迁移前完全对等。
- phase5 gate 26/45：**product:enhance-graph-submits-gpu + product:player-links-pipeline 双绿**。

### 下一条唯一任务

R3.3：probe 继续去重（bare-stage 诊断矩阵移出）+ headless `veyra_quality_probe` 骨架（链 veyra_pipeline+veyra_sources，无窗口跑 corpus，产出 gate JSON 契约），使 quality:runner-exe 具备通过条件。

## 2026-09-06 Goal session 2（Cycle 034-036）：产品库 R3.1 全部建立

### 执行摘要

接 session 1，完成 Playbook R3.1（四个产品库全部以真实成员建立并被 gate 检查放行）：

- **Cycle 034 R3.1a veyra_sinks**: WASAPI 音频（AudioPipeline 水位环形缓冲 + AudioRenderer 事件驱动 PTS 锚定主时钟 + 原子 seek）从 player_probe **逐字迁移**到 `veyra_sinks`（include/veyra/sink/WasapiAudioSink.h）。player_probe 3641→3128 行。行为验证无回归：underruns=0 overruns=0 seekCount=11、exit 12（已知 drift FAIL）不变。gate 新增 product:sinks/sources/player-links-sinks 检查并修复 player-links-pipeline 的跨 target 假阳性。
- **Cycle 035 R3.1b veyra_guidance**: IGuidanceProvider 接口 + **ZeroGuidanceProvider 真 GPU 实现**（三纹理 upload-copy 零初始化、GpuTextureHandle 完整生命周期字段、epoch 边界 requiresReset、provenance=Zero 诚实上报）。GPU 集成测试 13/13 双配置（真 RTX 5070，诊断 readback 验证全零）。测试自身曾有一个 staging 溢出 bug（分配 8 行复制 1080 行）——provider 本身正确，box 限定后全绿。
- **Cycle 036 R3.1c veyra_sources**: MediaFileSource 组合 veyra_media（无第二份解码实现）：Rational PTS 用真实流时基（实测 1/15360 单调）、Open/Seek/Discontinuity flags、单调 epoch、ColorDescription 解析 + assumed 默认（1080p→BT709 Limited 全 assumed）、原子 seek（demuxer+flush+flag）、EOS drain。corpus 驱动 23/23 双配置。修 3 轮：TRC 常量名、std::format 参数数（运行时 abort）、EOS 期望值。

### Gate 状态

phase5 gate 28/45 失败（exit 1 保持）。**产品库检查全绿**：pipeline/guidance/sinks/sources 四 target + player-links-sinks。剩余红项：quality-runner-target、player-links-pipeline、enhance-graph-submits-gpu（全部 R3.2 范围）+ 矩阵/depth/耐久（R3.2-R5 范围）。

### R3.2 迁移地图（供下个上下文）

- `processOneFrame` 位于 player_probe main.cpp:1982-~2470，~500 行 lambda，深度捕获 main() 作用域。
- 链路：ring.acquire(slot) → NV12 源（D3D12VA 纹理 fence-wait+双 plane SRV / 软件 sws→Nv12Upload dispatch）→ YuvToRgb dispatch 到 srcRgba → SR evaluate 或 ScaleBlit bypass 到 workRgba → ParityEncode → **submitAndSignal+新 list**（snippet 约束）→ NR evaluate（全参数块）→ ParityDecode 到 finalRgba → videoFrame[parity] blit + NVOF A/B 链（blit 传递）→ [后续未读：NVOF execute/densify/FG evaluate/presentQueue]。
- 迁移目标：src/pipeline/EnhanceGraph.cpp（gate 检查 ExecuteCommandLists+Evaluate 必须在此文件）；NvofGuidanceProvider 同批出生；player_probe 最终 <800 行（R3.3）。

### 下一条唯一任务

R3.2 EnhanceGraph 真实 GPU 链迁移（精确指针已写入 STATE.nextAction）。

## 2026-09-06 Goal session 1（Cycle 030-033）：接管、gate 重建、corpus、契约

### 执行摘要

新 Maker 按 GOAL_PROMPT 接管，完成 4 个原子 cycle，全部本地 checkpoint，无 push：

- **Cycle 030 R0 接管**: preflight 70/70；Release build exit 0；接管指纹 `61feb89b38e32f589b5c2fb6750526e5ad90dc3c`（44 entry）；四类窄 probe 新 run-id 复现基线（NVOF PASS / FG 59/59 / audio PASS / player FAIL driftP95=2858ms 复现已知缺陷）；44 项分类 keep/repair/hold 入 JOURNAL；保护性存档 `bb9c5361`（不含 MP4 删除，不写 lastGoodCommit）。
- **Cycle 031 R1.1 gate 重建并先红**: phase5.ps1 重写为 42 项 fail-closed 契约（删除 manifest-depth/第二次 1080p 冒充 4K/5 分钟冒充 30 分钟三个假通过口；新增产品库执行证明、本次 run extent/hash/timing/VRAM/reset JSON 契约、主路径纪律）。当前实现 exit 1，34/42 命名失败与 Playbook R1 逐项对应。存档 `7ec1e0c`。
- **Cycle 032 R1.2 确定性 corpus**: veyra_clip_gen 五场景（translation/occlusion/cut-flash-duplicate/particles/ui-text）× 1080p60/4K60 共 10 片 + SHA256 manifest。DLL 遮蔽问题（最小版 avcodec 遮蔽含 openh264 的 tools 版）用隔离运行目录 `out/build/x64-release/clipgen/` 解决。gate corpus:* 四项转绿，其余保持红（30/42）。存档 `c7d6414`。
- **Cycle 033 R2 契约族**: include/veyra/pipeline（Rational PTS 负值/未知、ColorDescription+assumed 标志+P010 fail-closed 路径、10 位 FrameFlags+breaksHistory、GpuTextureHandle ownerSlot/expectedState/readyFence、FrameWindow 固定 prev/current/next+lookaheadFrames≤2 无 vector、GuidanceFrame provenance/age/sourceSequence、ResetCoordinator 帧边界消费）。PipelineContractTests 50/50 Debug+Release；旧 unified 51/51 无回归。附带修复 descriptor_present_probe:617 debug C4702。存档 `07eb66d`。

### 实际命令（关键）

- `loop-gate.ps1 -Gate preflight` → 70/70 exit 0（session 首尾各一次）
- `build.ps1 -Preset x64-release` → exit 0（多轮）
- `veyra_nvof_probe` / `veyra_fg_harness --fg-test|--audio-test` / `veyra_player_probe --duration-seconds 20` → 0/0/0/12（logs/takeover-20260906/）
- `phase5.ps1 -Root .` → exit 1（34/42 → 30/42 两轮，失败清单见 JOURNAL 031/032）
- `veyra_clip_gen --make-corpus` → exit 0（隔离目录）
- `veyra_pipeline_tests` / `veyra_unified_tests` → 50/50、51/51（Debug+Release）

### 未执行/未通过

- Phase 5 gate 仍 exit 1（产品库/runner/真实 EnhanceGraph 未实现——这是 R3 的任务）；无 Phase 通过、无 Reviewer、无 lastGoodCommit 变更。
- player probe drift 2.8s 与 L2 GBV id=938 维持已知 FAIL（Phase 6 范围，未动）。
- 未运行 30 分钟耐久（runner 不存在，gate 正确拒绝）。

### 下一条唯一任务

R3.1：从 player_probe 抽取真实成员建立 veyra_sinks（WasapiAudioSink，~194-690 行）/veyra_guidance（NvofGuidanceProvider 包 NvOfSession）/veyra_sources（MediaFileSource），随后 R3.2 把 GPU 链移入 EnhanceGraph 使 `product:enhance-graph-submits-gpu` 检查具备通过条件。STATE.nextAction 已写入精确指针。

## 2026-09-06 强 Agent 接管审计与 Launch V1.3 重基线

### 用户决定

- 继续使用固定 hash 的实验 `nvngx_dlssnr.dll`/Feature 18 做本机研发，不等待尚未公开的通用 DLSS 5 SDK。
- 该决定不等于“效果与官方/Magpie 相同”已被证明，也不允许提交、打包或分发 runtime。
- 旧 Agent 错误过多；要求重写详细执行计划并交给更强 Agent。

### 对抗式审查结论

- Phase 0–4 的真实 checkpoint/日志保留。
- 历史 Phase 5 产品级 pass 撤销：`src/core/EnhanceGraph.cpp` 只复制 packet/增加 counter，注释写明实际 GPU pipeline 在 harness；旧 `phase5.ps1` 只因 depth manifest 存在就放行，并把第二次 1080p endurance 放在“4K60”检查位置。
- Phase 6 组件代码与证据保留：DLSSG 59/59 truth、NVOF、WASAPI、Present/teardown 修复均有价值；但 t10 所有 player JSON 仍为 FAIL，drift P95 约 2.8 秒，L2/GBV runtime 有 1700+ id=938 descriptor-uninitialized。
- `player_probe/main.cpp` 约 3641 行，真实 graph 尚未抽成共享产品库。
- 无 `apps/veyra` UI、CaptureCardSource、ImageExportSink、VideoExportSink 或真正 DAV2 provider。按完整 Launch V1 交付物估算进度约 40%±5%。
- 控制面修改前工作树约 31 个 status entry；本次文档重基线完成后为 44 个（增加的是计划/状态文件），且 tracked `validation/fixed_clips/test_h264_1080p.mp4` 仍处于删除状态；本轮未 reset/restore/删除任何旧 Agent 代码。
- NVOF SDK 实际已存在于 `third_party_local/nvidia/Optical_Flow_SDK_5.0.7`；旧 INBOX 缺失记录已作废。Video Codec SDK 13.1 与真实 4K60 采集硬件仍是外部阻塞。

### 文档/状态更新

- README、AGENTS、Product Spec、Playbook、Competitor Audit、Loop Engine、Goal/Review Prompt、gate contract 全部加入 2026-09-06 恢复口径。
- Playbook 新增唯一 R0→R12 施工顺序，精确规定工作树保护、phase5 gate 修复、共享 graph、guidance/depth、GBV/timing/drift、Player、Capture、Image/NVENC Export、UI/recovery 和最终 gate。
- BACKLOG 重新拆成可执行原子项；STATE 回到 Phase 5 `in_progress`，Phase 6/7 locked，`lastGoodCommit` 回到有效 Phase 4 checkpoint。
- GOAL_PROMPT 改为强 Agent 接管提示词，禁止从 UI 开始、禁止相信旧 pass、禁止清理未提交成果。

### 本轮实际命令

- `git status --short` / `git diff --stat` / `git diff --check` / `git log --oneline`：完成；发现上述 dirty tree，无 whitespace error。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`：控制面改动前 70/70，exit 0。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release`：exit 0，Ninja no work to do。
- 检查 `logs/phase6-manual/t10/*.json`：当前矩阵 verdict 全 FAIL；L0-r2 driftP95=2828.229ms、NVOF 450/0、FG 450、readback=0、自然 teardown。

### 未执行

- 本轮是计划/控制面审计，没有重新执行 RTX Feature 18/NVOF/DLSSG/player runtime；历史结果未冒充本轮结果。
- 未运行新的 phase5/phase6 gate、Reviewer 或 checkpoint；控制面 rehash/preflight 在文档修改完成后单独记录。

### 下一条唯一任务

新 Maker 执行 Playbook R0.1：重新取得工作树指纹并分类约 44 个未提交项，然后执行 R1.1，重写 phase5 gate 并在当前 metrics-only graph/假 4K/depth 缺口上证明 exit 1。

### 控制面收尾

- 重新计算 10 个受保护文件 SHA256，更新 `loop/CONTROL_HASHES.json`，并同步基础 `scripts/loop-gate.ps1` 的 manifest hash。
- 修改后再次运行 preflight：**70/70，exit 0**；STATE 当前 Phase 5 `in_progress`、Phase 6/7 locked、3 个 open P0/P1、5 个 blocker，状态机与 Git 指针检查全部通过。

## 2026-09-08 optimization blocked audit 3 — Goal blocked

Previous and current continuation classified no progress, not verified process waits. Read-only revalidation confirms unchanged README F226D0A7 / manifest expectation 781FAFD8, manifest hash 26559334; no explicit authorization for the exact two-hash synchronization. Same blocker for three consecutive Goal turns including startup. Under the explicit control-plane stop rule there is no permitted independent implementation remaining. Mark Goal blocked, retain full Q0–Q8 scope and unapplied proposal; no product/control edits, build or GPU test. Resume requires explicit authorization recorded in INBOX, then exact synchronization and fresh preflight. Not complete.

## 2026-09-08 Q1a RGB / odd dimensions and static NR isolation

Phase 7 optimization remains in progress. Q1b large-image tiling, Q2 zoom and Q3-Q7 quality work are not passed. Real capture remains unexecuted.

Changes: ResolutionPlan/EnhanceGraph accept odd extents up to actual single-texture 16384; WIC arbitrary 8192 guard removed with checked UINT byte bounds; RGB upload and RgbToLinear avoid 4:2:0 conversion; PNG negotiated BGRA packing fixed; ScaleBlit exact 1:1 load avoids long-image floating-point interpolation error. Static images skip NVOF and FG capability/Create/warmup, reject FG enable, normalize image settings to 1X. Product per-export integrity checks unchanged.

Build command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release (exit0; logs/optimization-goal-20260908/build-static-final.log).
Image command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File logs/optimization-goal-20260908/run-image-tests.ps1 (latest image-c17dd1e5813b494e9d25344ebd0c93e1, exit0,4.4185s; EXE 60EEF44E1E36B97182D0EE53A09A782444D7AA01864CD1B617D5677703CF9008).
Five NR-off cases 1x1,257x513,97x9001,4097x257,257x4097 have max RGB error0 and exact PNG readback. 257x513 and97x9001 real NR: Feature18 Create0x1 Success,handle non-null,SEH0,Evaluate1,nonblack output. FG capability/Create absent; direct enable and 2X apply rejected. This proves execution/dimensions, not NR quality equivalence.

Historical failures retained: image-ece8... missing shader dependency (fixed CMake); image-fe18... long1:1 error6 (fixed ScaleBlit). phase7-rgb failed old 23-frame assertion: actual 12source+11generated+1hold=24,120Hz,0.2sec,audio preserved. Developer delivery gate now checks all those identities/duration/rate for H264/HEVC; it does not change export behavior or add product scans.

Gate command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7. Q1a run4c37adc29e6e406c9f7e398fd2a286cd passed; independent reviewer run3af42697442443a6b79e3aab2e78c04b passed41.461s. Static-FG fix runb3c3df19f8b94d3eae6bcfde6aa68679 exit0 preceded final controller normalization/test assertions, so do not claim identical final executable coverage. Independent review_rgb_foundation scoped PASS with P2 static-FG finding subsequently fixed; this subsequent fix awaits review.

Next: Q1b bounded tiles using the same EnhanceGraph, full-size image export and viewport rendering. Inputs beyond single-texture limit still fail explicitly; memory/model/codec limits are real and arbitrary input support is not complete. No runtime replacement, push, artifact upload, packaging or shutdown.

## 2026-09-08 Q1b tiles and Q2 preview candidate (cycles 53–54)

Previous turn classified progress, not wait/no-progress. Fresh preflight passed; STOP absent. Q1a static FG isolation integrated into Q1b. Added TiledImageProcessor using shared EnhanceGraph (1280 core,128 context halo,64 overlap feather; spatial tiles reset independently). Real full-sized CPU result retained for image save. EngineControllerImage presents bounded viewport sampled from full result, reprocesses on settings only, supports original comparison, cancel and rollback. Normal presentation uses PreviewView UVs; professional wheel anchors cursor, middle pan/right reset; new media/daily reset fit. Product video export integrity unchanged.

Release build: scripts/build.ps1 -Root . -Preset x64-release, exit0; logs/optimization-goal-20260908/build-q1b-final.log. Current app SHA256 0409DACD716950F3B674D0B105AAC9972B1B85A8AAC8362EECAA27314166F0E6. Shader identities recorded separately; EXE hash alone does not prove shader identity.

Tests: image-55da6ef7d941450cbbb8d934852191a0, exit0,6.400s (run-image-tests.ps1,275s watchdog). NR-off single texture five sizes exact; 17001x17 and17x17001 tiled14 each, maxError0 including independent half-transparent white->188/transparent->black fixtures; cancellation after first tile returns no output. Real NR97x17001:14 tiles,14 Evaluates, full dimensions,PNG exact readback (pixel quality/equivalence not asserted). Earlier single257x513 and97x9001 real NR remain covered. UiContractTests pass includes pointer anchoring/inverse wheel/pan math.

Application --smoke-zoom --smoke-seconds 9/10 tests: zoom-large-5ae8f78c7c5442ca982871c5638d7c82 (17x17001 NRoff) saved same PNG SHA3E3331C3FF73E636F4F37467F8F0175EE2F1158772E24A5EB0AC1F1A69F525C1 despite zoom; zoom-nr-7918200c1f554761b4af22d7ffd68db4 normal257x513 NR1 unchanged; final app zoom-large-nr-0b7a2fdd8cef4cc1855094d3d3cedbe8 NR14 unchanged,97x17001 full save. Session/revision/output extent preserved; zoom/pan/reset/daily flags pass. App processes bounded25s. Runtime output/dimensions covered; screenshot pixel comparison and real pointer hover routing still need stronger evidence.

Final phase7: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7 exit0; logs/delivery/bb35fca4c4e64b269caa7a5eb4243a2d/result.json 41.140s, same app0409..., all software checks pass. Prior run0ff376...41.203s is older app1B943... . Software gate is not full Goal approval. Q1 video model/codec edge limits and tiled visual seams/quality still need review; Q3–Q7 remain. No capture hardware claim, publishing or shutdown.

### Cycle55 reviewer P2 recovery fix — 2026-09-08

review_tiles_zoom independently passed software gates (3637158bbc434cb3aa6af32f9d62935c40.906s; image-bb9100790cf54d5c85237c8afc1da9d0 6.465s) but scoped verdict FAIL for unhandled save allocation exception and presenter-before-drain unwind. Finding accepted and repaired, not dismissed by green gates.

All-exit cleanup guard now drains shared queue before presenter/graph destruction. Large-image save and settings exceptions retain last successful result; ordinary image saves also preserve playback. WIC channel packing is bounded to row bands instead of another full-size image; existing decode-back extent verification remains unchanged. Partial file is CREATE_NEW-owned and removed after WIC closes on failure, or disarmed after successful rename. No new integrity scans or video-export changes.

Fault injection is opt-in VEYRA_TEST_LARGE_IMAGE_SAVE_THROW=1, once after actual WIC WritePixels (partial exists), not actual system memory exhaustion. App save-recovery-15610c7429934b15803b3e0a5cbc6751 9s smoke/25s watchdog: exception caught; session/output retained; same-path retry success; no partial remains; saved SHA equals source3E3331...525C1. Zoom session/revision/output assertions still pass.

Build scripts/build.ps1 -Root . -Preset x64-release exit0 (build-save-recovery-final.log/build-save-tests.log). Final app SHA5E7BC723B647903851C4DD0265F6CDC1A16BBDCE9AEEF07D6E213808F49BE95E. Image integration image-98845305ab6045a79311ef503ecfee9d exit0,6.515s: prior cases, cancellation, new PNG/JPEG multiple-band1024x3073 solid RGB fixtures pass. Final phase7 via scripts/loop-gate.ps1 exit0; logs/delivery/04815e94852a435a83840eaa45ba0ac1/result.json41.246s matches final app. Reviewer recheck pending.

Read-only Q3 diagnosis: CaptureCardSource packet duration is still default known0; downstream clamp selects83333 ticks incorrectly. Source colorInfo defaults BT709/SD601 but graph reads unresolved AVFrame fields and uses different transfer/matrix defaults. Capture RGB32 also leaves transfer/matrix unspecified and alpha is not guaranteed meaningful. Next Q3 must resolve once, carry actual sample/nominal duration, and retain explicit fallback logging; no Q3 code changes yet.

Independent recheck PASS for implemented Q1a/Q1b/Q2 and cycle55 save recovery; see docs/REVIEW_IMAGES_PREVIEW_2026-09-08.md. Current Phase7/Goal remains in_progress. Prepare local checkpoint only; next close remaining input/preview evidence, then Q3–Q7.

## 2026-09-08 cycles56–61: input/preview evidence, duration and color

Prior turn progress checkpoint3b2806b; clean startup and preflight passed, no STOP. Phase7 optimization remains active.

Two-dimensional2561x2561 image tests:3x3 tiles,NRoff maxError0 including intersections;NRon9 real Evaluates. Final combined image matrix image-97a039da3acf457b91046ba4e93831e1 exit0,9.532s. Earlier image4a730...9.300s before color changes. This verifies coverage/execution; natural-image neural seam/context equivalence still not asserted.

Actual VideoPresenter GPU framebuffer geometry: preview-pixels-96eb8b4f9f9b44668dcfb3cb7a935ede,exit0,<1s/25s watchdog. Fit/zoom/pan/reference/reset pixel assertions passed; fit.png/zoom.png viewed. readPresentedFrameForTest is explicit diagnostics only and has no production callers. Normal playback/video export do not read pixels back. Root-window queued hover/focus test --smoke-hover: hover-0579d93ffdf14b3db425da4da6dfc553 exit0,9s,actual WindowFromPoint route with focus on ModeSwitch button;zoom/pan/reset/session/revision pass. Initial hover-fe036...failed because smoke checked in same timer before posted message could run;log shows actual route immediately afterwards. Fixed smoke's150ms post-message wait, not product logic.

Actual H264 YUV444 video import: video-dimensions-4790082c088444288795325b778e400a,257x513 and1280x2561 each6frames,NR6/NVOF5,app exit0,6s smoke/25s watchdog. Old even and2160-height import guards no longer block these inputs. Does not promise unlimited model/codec dimensions or extend export codec contract.

Q3 duration: CaptureTiming derives duration from complete IMediaSample GetTime;missing stop uses negotiated nominal;unknown stays unknown. FramePacket default duration is unknown. Scheduler rejects zero/invalid duration as measured interval and uses explicit nominal fallback;clamps before integer conversion. Unit14 checks pass:30/60fps,knownzero,missing/invalidsample,huge duration. Initial build-duration.log failed Windows max macro;parenthesized numeric_limits(max) fixed. No physical capture test in this run.

Q3 color: shared ColorMetadata resolver combines declared AVFrame fields with source fallback (per-frame explicit wins,assumed values adapt to decoded format),SD601/HD709,YUV709/RGBsRGB defaults with assumed logs. Source metadata and graph swscale/shader share it;player and video export pass packet colors. Captured RGB32 source reports same resolved metadata. Explicit BT2020 conversion rejected instead of misinterpreted as709;HDR policy unchanged. GPU YUV neutral128 gives142(BT709),130(sRGB),189(linear);SD601red254,0,0 within golden tolerance. File parser defaults now use resolver. Capture RGB still traverses NV12 and remains a Q3 optimization task.

Commands: scripts/build.ps1 -Root . -Preset x64-release exit0 (build-color-capture.log); veyra_live_timing_tests.exe14 PASS (duration-unit.log); run-image-tests.ps1 (275s bound) and explicit25s-bounded PreviewGeometry/app tests. Latest phase7 via scripts/loop-gate.ps1 -Gate phase7 exit0; logs/delivery/f3fbdc84e9c54e718e03a94066df28bc/result.json41.453s appCDD4A16CC66A462A907EFA9EB82C10D22AA58A7B057BAEFDFAF4FB0EC055DC4F. Earlier duration gate e2fee...41.489s was previous app9A5C... . Latest small metadata-format changes covered by gate; GPU color goldens precede those small changes and await independent rerun. No new export integrity checks, publish, runtime change or shutdown. Independent review pending; Q4–Q7 open.

Independent scoped review PASS: docs/REVIEW_COLOR_TIMING_2026-09-08.md; actual GPU tests and phase7 rerun. Q3 direct RGB capture and Q4-Q7 remain.

## Cycle62 — direct RGB capture (review pending)
- CaptureCardSource now labels DirectShow RGB32 as BGR0 (unused alpha); EngineController selects direct RGB for capture. EnhanceGraph accepts BGR0/RGB0 with opaque alpha and preserves alpha only for RGBA/BGRA. Removes an application RGB→NV12 conversion, not upstream device compression.
- Shared 64×36 scene/cadence analysis serves CPU YUV and live RGB. Static images remain single frame. No normal pixel readback added.
- Build: scripts/build.ps1 -Root project -Preset x64-release, build-capture-rgb2.log exit0. Initial image test 1df8045c failed because fixture omitted enableNvofStandalone; production sets it. Fixed fixture to match product, no production bypass.
- GPU image test image-1660346e66504f8bac949030915d7398 exit0, 11.706s (275s watchdog): alternating red/blue BGR0 alpha0 exact maxError8=0, opaque output, one detected cut; NR-on4 actual Evaluates and2 NVOF executes. Prior images/colors/tiles remain passed. This is synthetic source, not physical capture validation.
- powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7: 75 checks PASS, logs/delivery/8211c4bf969647d099f62cd3cd11d96a/result.json. App SHA256 410EE473BDBCD8948D24589D8047F4A54FA6C30BA4E243F2B946D44EAF03B06A.
- Q4–Q7 and natural image tile quality remain; full Phase7/Goal stays in_progress. Existing export integrity unchanged.

Cycle62 independent PASS: docs/REVIEW_CAPTURE_RGB_2026-09-08.md. phase7 afe022a9 41.407s, image9ac2822f11.487s. NR-on fixture error0 is unmeasured, not a pixel quality assertion. Next Q4: official DLSS guide31March2026 PDF pages20/35/37 verifies linear input IsHDR and exposure contract; local SR evaluate/isBypass still width-only (Create checks both), fix and GPU-test next.

## Cycle63 — SR linear input and extent contract
- DlssSrBackend now declares linear input via IsHDR|AutoExposure (0x41); preExposure/exposureScale1. Official NVIDIA DLSS Programming Guide31March2026, local DLSS_repo/doc PDF pages20,35,37 and installed310.7 SDK helpers confirm contract. Reference https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf . SDR product does not become HDR display output. No fabricated sampling jitter, MVLowRes flag or runtime replacement.
- Evaluate/isBypass now compare both dimensions, like Create; graph avoids creating SR for identical extents. Output-space motion pixels retained; zero-depth fallback remains explicitly unproven geometry.
- Actual GPU baseline image-576edf2d1fa145bfbfa7573cffb4e29213.081s unflagged256→512 gray interiors0 error. Fixed image-87b550aa803042169c9a4458e564b59216.462s: ordinary upscale gray error1, height-only256x128→256x256 error0, same-size bypass error0/evaluate0. Three real SR Evaluates for each upscale. Baseline/fixed lastframe whole RGB MAE0.1771/max62, mainly edges; not a general quality improvement claim. Height-only PNG visually inspected.
- Build scripts/build.ps1 -Root project -Preset x64-release logs build-sr-baseline/flags/diagnostic.log exit0. phase7-sr.log PASS75 checks logs/delivery/57facb26ad2d4776a9d5a37a8a814b80/result.json (before diagnostic-only fix).
- 1080→4K24frames SR/NR passed, but did not trigger60frame stats. Follow-up sr-motion60-bd4905c3be0b4b559dec8925030df785 FAILED76 GPU diagnostic errors: sampler used output4K extent on source1920x1080 flow/conf textures. Fixed tools/quality_probe/main.cpp sampler to read/validate actual resource dimensions/formats; no normal pipeline/gate relaxation.
- Final sr-motion60-c1220641df6b46d6afef93054dda5bf5/result.json exit0,5.8s,60sec watchdog: SR60,NR60,NVOF59,nonzeroMotion2292,GBV errors0,failures0,normal readback0. Source1920x1080/output3840x2160. Current app37361B9C23AEE741C11EA626DCF6C3D587DA73ACB848D2C1D1C1599EDE2A2445; qualityprobe57B89BC4A9CDB542A317E0060FD8ED1C477BECE5F203136D228EA4F5C94422FC.
- Review pending. Natural/known4K reconstruction, UI protection, motion/FG quality and candidate ROI remain. Export integrity unchanged; physical capture not executed.

Cycle63 independent scoped PASS: docs/REVIEW_SR_CONTRACT_2026-09-08.md. phase7 1168a76f41.678s/image0d5938b316.225s/SR60a70819e65.925s. Actual4K colorbars output visually inspected; not natural content. Next atomic Q5 GPU protected regions within residual composition, source-normalized coordinates, then professional rectangle selection/preset persistence; do not label private UI correction effective or forget SR/FG distinction.

## Cycles64–65 — manual NR protection and professional selection
- ProtectionSettings: up to4 source-normalized rectangles, finite/in-order validation and0..32 inward feather pixels, defaultdisabled. Residual GPU pass24 constants folds mask into existing NR delta composition; full protection returns base. No normal pixel readback, extra inference or per-frame CPU pixel processing.
- Player/new graph/settings rollback/export share protection fields. Large image tiles map source-normalized rectangles into padded tile coordinates (128halo exceeds32feather). Normalized regions survive output sizing; current feature explicitly protects NR changes only, not SR or FG.
- Preset schema2 serializes all protection fields; schema1 remains readable and upgrades on save. Unknown/corrupt files remain protected. Existing export integrity unchanged; worker structure size handshake retained.
- Professional controls: NR protection toggle/count, rectangle/clear buttons; visible notes maximum4, Esc, NR-only, preset persistence and source-change clearing. Layered selection outline only during drag; sourcePoint uses same zoom/pan geometry. Main Esc, mode change and resize cancel; source switch clears via new-session options even during master transaction. Existing private UI correction remains unverified.
- Build scripts/build.ps1 -Root project -Preset x64-release logs build-protection / presets / ui / ui-smoke / final / buttons all exit0. Replaced pair<int,int> sentinel with float to remove new conversion warnings.
- GPU image matrix image-a812c02d15ad45159670b325703a99fb19.450s/275watchdog PASS: real NR4; no-mask/empty/full/rectangle positive-negative comparisons show changed191323 channels, protectedError0 and outsideError0. Full-protected2561x2561 nine-tile NR maxError0. Existing SR/colors/oddimages/capture synthetic also pass. Feather blend visual/temporal quality not yet measured.
- veyra_repair_preset_tests.exe unique ignoredpath PASS: all fields, invalid region, legacy1 read→2write/reload, corrupt preservation. Unit test process under1s.
- phase7-protection.log0815b4cbc2dd4dd08d982cb1a0da302d PASS75. phase7-protection-ui.log b9de57f797724808a2b70df8fbe24239 PASS75,41.751s (app0A5CE... before smoke-only route refinement).
- Actual Win32 app --nr --no-fg --smoke-protection --smoke-seconds9 on257x513image (25s watchdog): protection-ui-ed8f1a8475fd4012826360032da1bdf3 PASS; final protection-buttons-329fc51ef4ed4cd3a1ebef948d7b7760 PASS uses actual SettingsWindow BM_CLICK handlers213/214, zoom2 rectangle bounds0.39898235,0.39979756,0.5989973,0.5991903, overlayvisible, unchangedSession, applied settings, clear/Esc PASS. Synthetic UI messages, not physical user gesture/visual style approval.
- Current app SHA256098F6950511922F20413DCCD4E05B86311A09D94743EE895C9B838E92A9756A1. Review pending. Q5 private resource experiments, SR/FG region policy, natural/moving/feathered quality, Q6/Q7 remain. Physical capture unexecuted.

Protection reviewer initial FAILP2: dirty numeric draft / unchanged revision master-off prevented checkbox/count populate. Fixed syncProtection independent of numericpopulate, calledtimerandsettingsEnabled. FinalactualUIprotection-indicators-cd408855c24d4741aa481db9a2329ba9 exit0 (9secondsmoke25watchdog): dirty0.42 draft retained throughrectangle/clear/Esc; master-offsame-revision regionon/off indicators correct, draftretained; masterrestored. Build-protection-indicators exit0; app0654C61F11D9FC50436B964F385E4028E6458C4B9D186B6DCD5B0796C383B4F5. Independent recheck pending; GPU code unchanged.

Final protection review scopedPASS afterP2fix: docs/REVIEW_NR_PROTECTION_2026-09-08.md. Independent finalphase7a178db7f41.477s/UIa20440899s PASS. CurrentmanualNR protection delivered locally; fullQ5/Goalnotcomplete. Next: feathered/partialtile/moving validation, private UIAlpha/Backbuffer/ControlMask isolated fixedruntime experiment; SR/FG referencepolicy; sourceNVOFcurrent->previous confirmed(NvOfSession inputB=current/referenceA=previous, densify negate0), Q6 bounds/photometricconfidence and Q7ROI thereafter.

## Cycle 66 - protection boundary diagnostics (2026-09-08)

Scoped test-only change in tests/integration/ImageDimensionTests.cpp: feather 2/32 pixels, disjoint regions, and partial 2561x2561 mask spanning central horizontal/vertical tile seams. No product code, export integrity or runtime changed.

Commands: scripts/build.ps1 -Root "$PWD" -Preset x64-release; scripts/loop-gate.ps1 -Gate preflight; logs/optimization-goal-20260908/run-image-tests.ps1 (275s watchdog); scripts/loop-gate.ps1 -Gate phase7. Build log build-protection-boundaries-final.log. Initial runs image-eeba5f9f5a934480ab91d3307fbdc74a (17.620s) and image-97deff3da67c4a3e94c731e561377964 (17.654s) failed a new test assertion: unsigned 192-x subtraction misclassified exterior pixels. Diagnostic per-mode evidence isolated the assertion; changed to 192.f-x, no product adjustment.

Maker image-262e170a7cef4014b6287f6c4c022f22 passed 21.828s. Independent review_protection_boundaries PASS with no introduced P0/P1/P2: preflight71; phase7 logs/delivery/d8846bbfe5d54b41b7b14327e8897975/result.json 41.695s/75checks; image-e8c7b9377cd94f68bd50fb189bd8b49c 21.916s. Actual Feature18 Create0x1 non-null SEH0; seven protection evaluations with original/baseline error0. Feather2/32 has 1581/15725 mixed channels, envelope error0. Partial nine-tile/nine-NR interior/exterior error0, envelope error1. Baseline comes from same run, dimensions verified.

Image-test SHA256 052F7DE99BB622DDF05F977D5431C8257F26330119CF7760072126C3DC655481. App remains 0654C61F11D9FC50436B964F385E4028E6458C4B9D186B6DCD5B0796C383B4F5. Reviewer tracked fingerprint f80219622037f7671d65046e14313267f9bb85ce unchanged.

Limits: these assertions prove endpoints and bounded nontrivial blending, not exact smoothstep or perceptual smoothness. Central seams covered, not every seam with a protected region. Moving HUD/temporal behavior, natural quality, SR/FG protection and private resource effectiveness remain unverified. Phase7 and full Goal remain in_progress. Next atomic task: moving-background/static-HUD protection matrix, then isolated optional NR resource contract; Q6 motion confidence/FG and Q7 candidate ROI remain pending.

## Cycles 67–68 - temporal protection and private-resource decision

Previous Goal turn was progress (3d73860 boundary evidence). Cycle67 normal temporal graph synthetic moving background/static HUD/caption replacements: final logs/optimization-goal-20260908/image-0f6adfcbc36b4eafb785d67f7a15e926/result.json26.004s PASS; 12NR/11NVOF/11motion frames per sequence, reset1/cut0, protected/outsideerror0. Positive controls changed1800777background and467773HUD channels;28350captionchannels genuinely changed to current input at frames4/8. Four actual captures saved per sequence; frame4 protected viewed. This is synthetic hard-mask proof, not natural footage or moving-mask/feather temporal quality.

Cycle68 adds an unset-by-default diagnostic NR parameter callback (EnhanceGraph.h/.cpp) and explicit optional-resource probe in ImageDimensionTests.cpp. No normal-path pixel readbacks, export checks or UI resource binding. Full private-resource expected contract FAIL is retained, not converted to PASS: R8 12.736s andRGBA8 12.912s exit1; Create/Evaluate successful/debug0, but ControlMask-one differs57 and UIAlpha-unprotected differs2 in final8-bit output. Raw protected output matches proxy while parity final differs8 from original. Diagnostic-state bug corrected; signedI32 andRGBA8 hypotheses did not fix mismatch. Full trail and primary citations: docs/NR_OPTIONAL_RESOURCE_FINDINGS_2026-09-08.md. Stop repeated hypotheses without new evidence; retain exact post-NR protection, proceed Q6. Final image executableF574508156F15E51B1E9FB932BBA3F96589E7B8A4B3DCA85DE71D046A3B6F6B0. Build via scripts/build.ps1 -Root "$PWD" -Preset x64-release, log build-nr-optional-format.log. Tests use run-image-tests.ps1 with275secondwatchdog; optional flags separately retain exit1. Independent review pending.

## Cycles67–68 independent review and checkpoint

review_nr_temporal_optional final scoped PASS; no introduced P0/P1/P2. Preflight71, phase7 logs/delivery/2b2498a58135471e981779c989390d31/result.json41.739s/75 checks; normal image6508c7ccffcc45c6a46ca158e14fe0d7 exit0/26.855s. Optional R8 image-d5f8d7387035495f95d22bc04d705857 exit1/12.753s andRGBA image-d997436007bd4a3c9214adbd550f0dd8 exit1/12.526s independently reproduce expectation mismatch with debug0. Optional failure is retained; no private resource integration approved. Reviewer actual frame4 protected PNG viewed; callback product-default-disabled and resource lifetime/state transitions verified. Diff fingerprint9bdf08fe0712eb335c4e1f027b450b2a3de0cfd8 unchanged. AppB48EB58A70FCB1B25014AD27688B7A3DA90058DD2BD8A0ED4AC383E57E2D0F1C, imageEXEF574508156F15E51B1E9FB932BBA3F96589E7B8A4B3DCA85DE71D046A3B6F6B0.

Existing P2 diagnostic gap found: tests/integration/RepairShaderTests.cpp still uses8 residual constants, actual shader requires24. Current graph image matrix uses24 correctly, but that old standalone harness cannot prove the current contract. Next atomic task fixes and validates this harness, then Q6 adds bounds/photometric motion checks and measuresFG; Q7 and natural comparisons remain open. FullGoal/Phase7in_progress; no practical capture/long-term/distribution acceptance claimed.

## Cycles69–70 - residual harness and fused motion validation

Cycle69 fixes existing P2 RepairShaderTests root constant layout8->24; actual GPU downsample and residual identity at strengths0/1/2 PASS, log shader-cycle69.log (60s watchdog); build-cycle69.log; gate-cycle69.log75PASS. Prior turn classified progress d2921d2.

Cycle70 NvofDensify now uses existing previous/current source-space encoded RGB with raw current->previous vectors. Cost>=32 rejects as before. When validation enabled, out-of-bounds reprojection clears confidence/motion; bilinear previous-luma vs current-luma mismatch smoothsteps trust from error.03 to.15, attenuating motion and existing cost confidence. This is a bounded heuristic, not a probability or safe history-exclusion guarantee. It fuses into existing dispatch (4SRV+2UAV); graph transitions A/B toSRV thenCOMMON after NVOF fence synchronization. No new textures, models, CPU readbacks or source-frame waiting. Default enabled; --legacy-motion is quality-probe-only A/B with distinct configHash and motionValidation0/3 JSON.

Standalone GPU matrix (before product GPU integration): known+2,+2.5,-2.5pixel displacement, high-cost cells, both-side out-of-frame, strong mismatch and partial mismatch. Final motion-shader-final.log: legacy128rejected/896retained; validated416rejected/408retained/200attenuated; errors0. Source geometry analytically known, not inferred fromNR outputs. Build-motion-validation/final/fractional.log exit0.

Actual SR60/NVOF59/NR60 plusGBV: motion-final-158d0996302841c68344adf749bda65f vs motion-final-legacy-af959ca76cb740a19ccada074c5d6a13, failures0. GPU command-list P50 3.7572vs3.8628ms, P95 24.8223vs24.5440ms, duration5.3vs5.5s; these noisy mixed-command measurements do not prove speedup or isolated shader cost. VRAM headroom7882MiB both. Normal image+temporal protection matrix image-e306179d8fa74490a54d50745f69db3a27.397s PASS; protected/outsideerror0 and freshcaption28350 retained. Each invocation<=275s. Export integrity unchanged. Final gate/review pending. Q6 still needs occlusion/scene/FG/natural corpus and A/B bidirectional ROI; Q7 remains pending.

## Cycles69–70 independent review

review_motion_validation scopedPASS, no introducedP0/P1/P2. Independent preflight71; phase7 logs/delivery/240031138e804d7f84a42a57a2069759/result.json41.996s/75PASS. Initial reviewer environment missingWindowsPowerShellmodulepath/Get-FileHash fixed; first failure retained, not product failure. Motion/repaired residual GPU tests PASS, logs review-motion_validation.out.log and review-repair_shader.out.log. Image29e690f3e6ba4492aab270cbbdefc06825.692s PASS, current protected/outsideerror0/captionfresh28350. Quality review-motion-2041c730446f48f9a4a5f846d13e8074 and legacy045036253b284931b5d5f3a7b4f61d98 bothSR60/NR60/NVOF59/GBV0/failures0,5.4/5.5s; Create0x1Success/SEH0. No speedup claim. All paths under logs/optimization-goal-20260908 unless explicit delivery.

AppD14DFB94A933E1BE85B9972CADB628BC58E320C26D53B4887F2102BFA2E48D46; quality21A265B0645BD8FA5556E527776155FD26AB7F65DF2EBAC9E523CF653D0988F1; motiontestC5F3AE03B74EB56B106C070FD275095811DE0CE18599411B564C1F2B85C25428. Reviewer trackedfingerprinte3db68bd6d230d63b0752d9f5d6fa84be6837252 unchanged. Product export integrity remains unchanged.

Limits: encoded-luma.03/.15 heuristic cannot detect isoluminant mismatch or repetitive wrong matches; shortened/zero motion does not prove safe model history exclusion. Unit matrix currently horizontalpositive/negative/fractional only; next atomic task extends nonzerovertical/top-bottom/cost31-32/intermediateconfidence, then actual occlusion/cut/FG comparisons and A/B bidirectional cost/benefit. Natural footage/Q7/physical acceptance remain pending. Phase7/Goalin_progress; scopedpass only.

## Cycles71-73 scoped scene repair candidate
See docs/SCENE_MOTION_CORPUS_2026-09-08.md. Motion vertical/cost GPU checks and scene14 checks PASS. Fixed all3 authored cuts missed by old .3 SAD threshold; actual shared graph logs150/300/301/302/450,600NR/594NVOF/debug0. Four other600frameclips zero new boundaries. Actual1080p2X export fg-scene-b1e15c6c88b64b00aee2b6af5f8c4990 exit0:600source/594generated/6hold/1200output; full diagnosticdecode1200,5boundaryholds matchpreviousYmean<=.049/255. Buildcycle73exit0. No exportintegrity change, no naturalquality conclusion. Frozen gate/review pending.

## Cycles71-73 independent scoped PASS
review_scene_boundaries no introducedP0/P1/P2; docs/REVIEW_SCENE_BOUNDARIES_2026-09-08.md. Independentpreflight71/phase7delivery55725c1cd58846c59342d5e62456277341.568s75PASS. Freshreview-scene-fc558d881d0b404fbe51e5f6f2ebd6e0:scene14/motionbothaxesdebug0;RTXNR600NVOF594/Create0x1SEH0;actualFG600source594generated6holds1200output andindependentfull1200decode. BoundarypreviousYerror<=.048694/255. Trackedfingerprint4343fa50af201ad1393b6bec4228e3c18fb4447dunchanged. Q6/Q7/natural/physical/long-term/distribution not passed.

## Cycle74 bidirectional candidate evidence
Shared optional BOTH session and realGPU diagnostic; defaults unchanged. docs/BIDIRECTIONAL_FLOW_EXPERIMENT_2026-09-08.md. Finalbidir-matrix-e47ef77e21b44baaa4eace596d6080cd360p/1080p/4Kall exit0/debug0, known+8/-8EPE.0442px.1080pnovelwrongaccept6330->12, correctbackground101392unchanged;4K25992->44/correct432992unchanged. Fixed-pair warmedthroughput1080p.836->1.510ms,4K2.974->5.571ms; notlatency/P95/naturalquality. Default remainsforward; no graph/exportintegrity/newmodel changes. Frozen gate/review pending.

## Cycle74 independent final scopedPASS
review_bidirectional_candidate: initialP2diagnosticfootprintalignmentFAIL repaired; independent640x129all4casesPASS/debug0 (novel191->73,good1411unchanged). Initial1080/4KindependentmatrixPASS remainsvalid. Finalphase7aea3d88148bf4e6b9c793cd99f95f7fb75PASSexit0; docs/REVIEW_BIDIRECTIONAL_FLOW_2026-09-08.md. Reviewerfinalfingerprint2d3745d722cd9931e12d9ec6200d14d4d66f9924unchanged. Optionalsharedcapabilityonly; defaultforward, no graph/shader/exportintegrity/model changes. Retaincandidatependingnatural/changingframeNR/FGROI; fullGoalopen.

## Cycle75 NR depth controlled response candidate
Newdiagnostictarget only. docs/NR_DEPTH_RESPONSE_2026-09-08.md; finaldepth-response-b8490565d1374d1c9a28a93ecc12b54dexit0/39.416s/debug0. Default/replacement/residentdepth0,1,gradient,checker:rawNRandfinalRGBchanged0;singleframe andall12temporalframes. Intensity0positiveandMVscale0/-1changemillionsofRGBchannels; explicit.5/repeatbaselineexact;NR12/NVOF11/motion11/reset1. Resident originaltexturecontentsmutated afterframe0 toexclude simplepointercacheexplanation. No basisfordefaultNRdepthmodel; SR/FGdepth nottested. Frozen gate/review pending.

## Cycle75 independent scopedPASS
review_nr_depth_response nointroducedP0/P1/P2. Independentpreflight71/phase7e67dc13783b24976a68fd1118c43a18841.694s75PASS. Actualreview-depth-9678e01163c949b999231759f1fe6fc6exit0/40.440s:all24casesreproduce,depthzerochange/intensityandMVpositive,NR12NVOF11motion11reset1/debug0. Raw/residentcopy/pointer/per-frame comparisonreviewed; no universaldepthignoredclaim. docs/REVIEW_NR_DEPTH_RESPONSE_2026-09-08.md. Trackedfingerprintca90e967b7f2c588749b2eaf8dc516fed4b7eaadunchanged; exe0A0CA21053F6CDE55CB023A6AD3D71A24EEEE1C91BA6B91E2E57B894A4D8859A. Do notadddefaultNRdepthmodelwithoutnewbenefitevidence. SR/FGdepth/naturalreconstructionremainopen.

## Cycle76 candidate final matrix
18 cases actual RTX PASS, srfg-final-b3590fc9f2b1478ab4e205897cb2ef2f exit0 31.988s/debug0; SR12 or FG11/NVOF11/reset1; all generated PTS checked. SR depth RGB unchanged, FG ordinal near-depth MAE .237438->.237343, insufficient default model justification. docs/SR_FG_DEPTH_RESPONSE_2026-09-08.md. Build final exit0; frozen gate/review pending. Export integrity unchanged.

## Cycle76 independent final PASS
Initial P2 incomplete-FG-sample assertion repaired; independent18cases review-srfg-final-e1efc4f1b8124f8f9bea0aa7423b8345 exit0/32.210s, actual Create/Evaluate0x1 SEH0/debug0. Finalpreflight71 and phase7 bf527e55e4914c8aa93451ec78121f18 41.408s/75PASS. docs/REVIEW_SR_FG_DEPTH_RESPONSE_2026-09-08.md. Defaultdepthmodel remains absent; no verified cost/quality case for adding it. Next: known4K reconstruction against spatial baseline, then natural/changing-frame quality. RTX Video SDK absent from project and filename-targeted Downloads search. Export integrity unchanged; fullGoal open.

## Cycle77 reconstruction candidate
Actual1080->4K sharedgraph two24frame scenes: final0-b240e19c658c43d2bc06b06c4f4442ea17.030s andfinal1-42eb27fb292f4be0814b1e6d2c42de0819.956s exit0, NR/FGoff SR24/NVOF23/reset1/debug0, SRrepeat exact. SR lowers spatialMAE but increases temporalerrorandmaxerror; docs/SR_RECONSTRUCTION_2026-09-08.md. No naturalquality or defaultreplacement claim. Buildcycle77finalexit0; frozen gate/reviewpending.

## Cycle77 independent final PASS
review_sr_reconstruction: full24frameSR/spatial metrics reproduced, SRrepeat exact; twoactualRTXinvocations17.861/22.122s, debug0; docs/REVIEW_SR_RECONSTRUCTION_2026-09-08.md. Independentpreflight71/phase7 268ee4b316bf4604bedafcd9b747b09c41.718s75PASS. DocumentationP2 initializationFGwarmup distinction fixedandreadonlyrereadclosed. No default/product/export change. Next isolate exactmotion vsestimated SR guidance, and natural material; recent-app source exists (ffprobe H2641920x1080,30000/1001,limitedBT709,56.689s), not native4K reference. FullGoalopen.

## Cycle78 SR motion response candidate
Actual two8mode 4K matrices final0-978ea39fa951411f9a3f9d82f2239c4148.652s andfinal1-f755274a89804bd19d4da245b3a712b255.509s exit0; resource width/format/disabled negativecontrols reject; SR24/NVOF23/reset1/debug0 andestimated/exact repeats identical. Exactmotion improveserrors, zero/inverseworsen spatial; defaultfilter slightlyworse thanlegacy onbothsyntheticcases. docs/SR_MOTION_RESPONSE_2026-09-08.md. NewSR-only borrowedprobe unsetinproduct; no defaults/export change. Frozen gate/reviewpending.

## Cycle79 — professional move/resize callback stack repair (2026-09-08)
User WER dump 33456: C000041D at module RVA BB0F7, same-object linker MAP identifies __chkstk; AppShell WndProc reserves0x36A28 bytes, nested layout/control/modal callbacks exhaust stack. Move 32768-wchar per-operation path buffers to vector heap storage; retain path capacity and independent modal lifetime. Fixed compiler reservation0x6A48 bytes. No STACK linker increase or exception swallowing.
Build: scripts/build.ps1 -Root <project> -Preset x64-release, exit0 (logs/optimization-goal-20260908/build-cycle79.log). Native bounded regression scripts/acceptance/ui-window-resize.py: empty18.265s (resize-1788881542706216600), playback18.578s (resize-1788881522216906600), each24moves/24sizes/4selectors, clean shutdown. Playback600NR/599NVOF, failed=false, P951.63ms. These are Win32 scripted operations, not physical mouse acceptance. Early old-binary probe did not reliably reproduce crash; old exit0 without smoke marker is not accepted evidence; final script requires actual completed smoke.
Preflight71PASS; phase7FAIL player-sync in logs/delivery/966863bd15a54bd58849984c73b5105d/result.json. Prior independentCycle78 alsoFAIL54.38ms, preceding this UI edit. No overall gate pass or latency fix claimed. Review pending; no checkpoint marking product passed.

Cycle79 final independent scopedPASS: review_window_stack found no remainingP0/P1/P2 in product fix or regression. Final empty regression logs/optimization-goal-20260908/resize-1788881908641248600/result.json:18.234s,24moves,24sizes,4selectoropens,cleanSmoke=true,exit0. Prior same-binary playback resize-1788881714808561400:18.594s,600NR/599NVOF,failed=false. SHA25686E1EE322C86B539FC7C280F10726AC721F05FF322D5C0CA778EE1F9608C5D45. External25s supervisor+5s cleanup; forced timeout inspected not fault-injected. User mouse acceptance remains; full Phase7 not passed, no product-completion checkpoint. Latest gate concurrent usercapture means no clean latency conclusion.

Cycle80 capture whitelist removed and labels identify native subtype. ActualUSB SS(noSSPlus), firmware UVC2x12 frame entries no1440p. 4K18 YUY2/MJPEG source testsPASS andapp107NR106NVOF,callback18.02fps,0drops. Details/commands/failures docs/CAPTURE_CARD_AUDIT_2026-09-08.md. User resumedcapture0:22:0 after update; no further exclusivehardware tests while in use. Independent scopedreview pending.

Cycle80 independent scopedPASS: metadata list + offline rawUSBdescriptor independently verified, code and actualsource/NRlogs agree, noP0/P1/P2. Preflight71PASS. FullPhase7notretested while user4Kcaptureactive; priorlatenessissueunresolved. Finalexe2587E014D349402254C23C8B82108539B4C586E2C4C1D2C558BC636EB4AB34EB. Audit report contains complete evidence.

## Cycle81 — video SDK trial 2026-09-09
RTX5070 FRUC standalone natural-pan test generated 3 distinct intermediates; Create/Register/Process/Unregister/Destroy=0. Synthetic tests repeated frames and correctly failed. Evidence and build commands: docs/RTX_VIDEO_FRUC_TRIAL_2026-09-09.md. Each invocation external25s watchdog, final exit0. Player PID20272 remained running, no performance conclusion. RTX Video SDK requires official login and was not executed. No product changes or phase advancement; existing Phase7 timing failure remains. Next: SDK acquisition and isolated comparison before integration.

## Cycle82 — RTX Video D3D12 trial
User SDK received and safely extracted ignored. Actual RTX5070 Init/Create/Evaluate0x1 Available1; 1080p->4K all5quality outputs nonblack/different. Warm static8sample GPU medians q1=1.374ms q2=1.774ms q4=5.857ms; not end-to-end/capture comparison. Debug errors0 with retained warnings. Build/readback/timing/logs/limits in docs/RTX_VIDEO_FRUC_TRIAL_2026-09-09.md. Preflight71PASS; per-process25s watchdog. No product code/default changes, Phase7 timing failure unchanged. Next optional shared backend and same-source A/B, not automatic default replacement.

## Cycle83 independent final scoped PASS
Independent review_video_sr: no remaining introduced P0/P1/P2; original backend-switch P1 and draft-loss P2 closed. Independent low->medium->DLSS->low:3 applied transitions, VSR Create/Evaluate/Release3/401/3 all0x1/SEH0; DLSS Create1. Draft0.314159 retained across all3 changes, never secretly applied. Preset v2 protection/feather and defaultquality0 retained; v3quality2 roundtrip and invalid5 rejection pass.
Preflight71PASS; phase7 delivery a76caae2b03b4a96aaed4e4e1ab4eb71 45.295s/75PASS. Initial reviewer environment PSModulePath failure retained; fixed module search and reran. Evidence logs/video-sdk-trial-20260909/review-ui-switch.stdout.log, review-app-switch.log, review-preset.log. Tracked fingerprint98da728f82273048c095e9a12b2fe63c3114def9 unchanged during review; no tracked proprietary assets.
Final exeSHA37574BF6ACCA4E78A26EF10BE0D3AD0CDD90470D8A2353C6D551ACC798006A5B. User can choose Professional/Enhancement/RTX Video SR low or medium, enable SR with realtimeNR; test FGoff then2X. Physicalcard not enumerated this turn, onlyOBS; physical capture/naturalquality/longstability/unresolved historical intermittenttiming remain unaccepted. No overallGoal completion or distribution approval; no new integratedGBV claim.

## Cycle84 physical capture diagnosis
Same userPID22300 USB3Video1920x1080YUY2/60 examined without reopening. NativeNR+highestVSR is bottleneck: NR22.911ms vs realtime6.100ms; same-source realtime/low restores59.51fps vs native/highabout30fps. Temporary q4/realtime,q1/realtime,q1/realtime/FGoff comparisons each13s; originalquality4/nativeNR/FG2 restored. No code/binary change. NRdial is not total latency; rolling1200sample P95 mixes prior settings, so noFGofflatency conclusion frommixedwindow. Report docs/PHYSICAL_CAPTURE_DIAGNOSIS_2026-09-09.md; evidence logs/video-sdk-trial-20260909/physical-*. FullGoal open. Next revise metric windows/UI clarity and user chooses realtime/low forlive60fps.


## Cycle85 — settings page submission isolation: independent PASS
User anomaly log revision29 NR3840x2160 median22.135ms -> revision30 NR1920x1080 median6.154ms; RTX Video SR quality1/output4K unchanged. Historical exact clicks not logged. SettingsWindow FG apply previously submitted hidden enhancement fields; now FG and enhancement reads/submissions and dirty drafts are isolated. No GPU stage order change.
Changed product file: apps/veyra/SettingsWindow.cpp. Regression: scripts/acceptance/ui-settings-page-scope.py. Build command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <project> -Preset x64-release; PASS, logs/video-sdk-trial-20260909/build-cycle85.log. Maker and independent command: python scripts/acceptance/ui-settings-page-scope.py; PASS (independent stdout logs/video-sdk-trial-20260909/review85-ui.stdout.log; app logs/settings-page-scope-72281e89bf974ce3a6f143945181cf2e.log). Independent actual revision4 intensity1/multiplier2 keeps nativeNR; revision5 applies realtimeNR with fg=1 and435 valid generated frames.
Independent commands: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate preflight (71 PASS), same command -Gate phase7 (75 PASS,42.678s), logs/delivery/c2bd451c2bb844448b60e78a61714558/result.json. Reviewer review_video_sr: no new P0/P1/P2; no tracked writes. Frozen code fingerprint8f3ebcf6d5f68c02592f52a0194674b7b3f776ae. EXE SHA25688AE23C12616355858AE85A6F8D11E0AC441872A5117665578DF626CAA48306A. Actual RTX execution performed; proprietary assets remain untracked.
Correction superseding Cycle84 restoration claim: nativeNR and VSRquality4 restored, but originalFG2 was left OFF (revision22 multiplier1). Hidden legacy checkbox was stale and was an invalid restoration source. User notified; future comparisons must use applied settings.
Phase7 overall optimization remains in_progress. Scope PASS does not resolve historical intermittent timing or prove physical display latency. NR dial measures NR stage only; rolling windows still mix settings. Next task: configuration-specific timing windows and unambiguous metric display, followed by user real-card acceptance. User app was closed normally for rebuild; reopen updated app for real-card retest.


Cycle86 safety stop: build passed (logs/video-sdk-trial-20260909/build-cycle86.log), but preflight reports file:loop/GOAL_PROMPT.md and control:loop/GOAL_PROMPT.md missing. This deletion was present before Cycle86 edits. Per AGENTS control-plane rule, stopped before GPU regression, independent review and checkpoint; no manifest/gate rebaseline and no restoration of user deletion. UI changes remain unverified candidate. Ask user to authorize restoring the exact tracked GOAL_PROMPT.md from HEAD, then resume verification/documentation/local checkpoint.


## Cycle86 最终独立验收与交接

只读review_video_sr限定范围PASS，无新增P0/P1/P2。preflight69 PASS，phase7 73 PASS，实际命令 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate preflight` / `-Gate phase7`；delivery `logs/delivery/9f7468b334bc457db4b8bf125d4f5ddb/result.json`。减少的2项仅为用户明确删除的GOAL_PROMPT文件存在/hash检查，剩余10个控制文件身份保持。

实际RTX独立UI回归通过（`python scripts/acceptance/ui-settings-page-scope.py`）；额外回归确认master-off保存.55/style1/FG2、重新开启真实应用；焦点内无效文本保留，失焦恢复；注入style2失败后回滚style1且按钮状态同步；滚轮确实滚页但不调参；还原默认生效。证据 `logs/video-sdk-trial-20260909/review86-ui.stdout.log`、`review86-extra-final.stdout.log`、`logs/settings-page-scope-79d029d4883e42fea08d53eecfcc65c6.log`。初次补测1秒等待不足，改为最长5秒观察真实回滚后通过，未用固定短等待误报产品失败。

EXE SHA256: `3BD470D51AD43F7C461F23AB6F9F9B7234DC6F33D84C9DDEB35A463663264E2C`。构建日志 `logs/video-sdk-trial-20260909/build-cycle86.log`；独立review前后跟踪指纹 `9d45fe7ff6b19a67b3a42db2215928b1aecf5b4f`不变，之后仅更新交接记录。单次测试均不足300秒。Cycle86修复可交本机实测；整体Phase7优化仍in_progress，既有统计窗口/间歇时序/实卡体验/长期稳定风险未宣称消失。下一任务见HANDOFF优先处理配置相关统计窗口。

本地Git checkpoint包括Cycles78–86已核对源码、使用指南、交接报告与授权的GOAL_PROMPT删除；不包含runtime/SDK/个人媒体，不push。实际提交ID以本次 `git log -1` 为准，避免在commit内容中制造自指hash。


## Cycle87–88 FRUC/high-FPS candidate

User requested1000fps support andFRUC2/3/4. Source timing admission120→1000, exact timestamp validation retained. OptionalFRUC behind sharedgraph, UI/preset/export propagated, DLSS retained. Same-process multiinstance required per-instanceCUDAcontext; destroy/recreate repeatedlyRegister4/warpSEH, so stopped that approach and isolated runtime in hidden ownedworker with GPUsharedtextures/fence. No normal pixelreadback. Fullreset freshworker is correct in triple-reset tests but has startupcost/capture-drop feedbackrisk. Maker pixel/PTS2/3/4PASS; playbackknownpan有效5/10/15子帧与实际播放计数、NVENC24/36/48frame输出、1000fps2000frames/999.34processedfps及UI切换PASS. Specificcommands/logs/failures in docs/FRUC_INTEGRATION_2026-09-09.md. 最终lazy创建候选等待独立review/preflight/phase7；整体Phase7未完成，不push。


## 独立检查与用户反馈补充

独立review_fruc已报告CPU16+27项/preset、实际2/3/4pixel/PTS/连续reset、UI全后端切换和自有worker进程故障测试通过，无新增已确认P0/P1/P2消息。preflight69 PASS；其实际phase7日志 `logs/video-sdk-trial-20260909/review-fruc-gate-phase7.log` 为73 PASS，delivery `logs/delivery/1c195ef6d3684879b220fc8c13439e83/result.json`。Reviewer随后额度耗尽，未返回最终整体结论；因此记录为independent_checks_passed / final_verdict_unavailable，不伪造最终Reviewer PASS。用户随后明确“我已经测试没问题”，记录用户本次体验通过，不外推其未说明的配置与长期稳定性。程序SHA256 BD76BA1F3D66DF30286E450AD1742E773F48989B5815C175BDC0A297ABF4E94D；workerSHA256 B6F267C3B1C73DE0CB99C4BAD4B5DA9355C42DF7E7F37F21C9891B02B80412A3。


## 用户新请求：GitHub性能归因

已固定7个仓库head，只读源码/官方文档+既有GPU日志；报告docs/GITHUB_PERFORMANCE_AUDIT_2026-09-09.md。确认原生4KNR硬预算与软件同步/日志/重置/统计问题并存，不提供未测占比。未运行竞品、未改runtime/产品代码、未占实卡。下一条任务：配置revision隔离统计，再同源逐项A/B定位等待与计算；不先建议换卡。

## 2026-09-09 竞品对照改进方案（仅文档）

用户暂停间歇卡顿现场追查，要求先对照已有竞品写改进方案。新增 docs/COMPETITOR_IMPROVEMENT_PLAN_2026-09-09.md，按固定 GitHub 审计、当前 controller/FRUC 源码、旧质量方案及已完成专项整理。先统计/日志/有界调度与 reset，再做 CUDA FRUC、硬解和拷贝消减实验；深度/缓存不加入默认路径。明确每项证据、动作、验收与回退，保留导出完整性检查和单次 300 秒约定。最新自然日志未包含用户所述卡顿的处理证据，不给周期性卡顿下确定归因。同步 HANDOFF 索引和 STATE 文档条目，未改产品代码/依赖/默认设置或保护门禁。未运行新的构建/GPU/实卡测试，未推进 Phase 7 或补造 Reviewer 结论。

## 2026-09-09 0.0.1 发布候选（尚未提交或上传）

按用户请求完成 README 中英文重写、0.0.1 release notes、可复现便携包脚本，以及运行目录自定位。应用从 EXE 所在目录查找 shader、日志、设置和可选本地运行时；源码开发目录仍兼容。Release build 命令 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <project> -Preset x64-release` 通过。最终 ZIP `out/releases/Veyra-0.0.1-win64-portable.zip` SHA256 `906964FC80BA74F5A8407AE8E4FD9890485962539FD59FAF7764CF74799D0095`；从新解压目录以 `--smoke-empty --smoke-seconds 3 --no-nr --no-sr --no-fg` 实际启动退出码 0。包内未发现 NVIDIA、CUDA、NVENC、PDB 或 LIB；包含 Veyra EXE、自建 worker、shader、FFmpeg shared DLL、FFmpeg copyright、项目 LICENSE/notice 和空的 runtime_local/nvidia 说明。README 是控制面文件，preflight 因其固定 hash 变更而失败，未私自更新控制清单。公开上传仍等待许可证/来源确认及远端 main 分支整合；未创建 commit、tag、release 或 push。

## 2026-09-09 Runtime Pack 发布策略（用户明确授权）

用户选择完整开箱即用的实验 Runtime Pack，并明确接受该 Pack 可能被 GitHub 下架或被权利方要求移除的风险。`AGENTS.md` 已锁定源码/Release 物理隔离：NVIDIA 二进制、SDK、头文件、库、样例和压缩包不得进入 Git、LFS、源码或资源；仅经 manifest 白名单、固定 SHA-256、有效 NVIDIA Authenticode 签名和适用许可证检查的用户包 Release 资产可携带运行时。不得篡改/重签名/隐藏、不得从游戏或驱动缓存提取，且不得宣称 NVIDIA 官方支持。

`FrucWorker` 改为从 EXE 相对的 `runtime_local/nvidia/NvOFFRUC.dll` 加载，移除开发机绝对路径。`scripts/package-portable.ps1` 以显式 allowlist 打包 `nvngx_dlss.dll`、`nvngx_dlssg.dll`、`nvngx_dlssnr.dll`、`nvngx_vsr.dll`、`NvOFFRUC.dll`，生成 `release-runtime-manifest.json`，附带 RTX / Optical Flow 许可证并排除 SDK 开发文件。Release 构建通过；最新 ZIP SHA256 `C3BA5C6E138557D3F4A208737CCC2542CE5687DA1BA7F0AD534F009DA4AE1E6F`。独立解压后五个 DLL 的 SHA-256 均匹配 manifest、Authenticode 均为 Valid，包内未发现 PDB/LIB/头文件/样例/SDK 目录；`veyra.exe --smoke-empty --smoke-seconds 3 --no-nr --no-sr --no-fg` 退出码 0。尚未 commit、push、tag 或创建 GitHub Release；README 控制面 hash 漂移仍未自行重基线。

## 2026-09-09 OBS/Magpie 实卡反馈：基础画质审计与修复交接

交付 docs/BASELINE_QUALITY_PERFORMANCE_REPAIR_2026-09-09.md，优先级改为先找色带首次失真节点，再比较同尺寸性能。确认 DirectShow 强制 RGB32 与原生颜色元数据缺失、固定 SR 4K 目标、处理/呈现串行、逐帧复制/日志等设计成本；不宣称已证明具体转换器用了错误矩阵。保存用户 OBS/Magpie 日志、精确源码与身份于 gitignored 的 logs/obs-magpie-rootcause-20260909/。Magpie 现有日志多为 1440p，不能冒充与 Veyra 同等 4K 实测。

实际命令：python logs/obs-magpie-rootcause-20260909/build_probe.py。复用当前编译的产品库/Shader，构建仅诊断入口；build/run 均 exit 0，IMAGE_DIMENSION 256x64 nr=0 maxError8=0，CAPTURE_RGB nr=0 maxError8=0 cuts=1 nvof=0。首次包装脚本匹配编译参数失败，修正后约 5.4 秒完成。此结果仅覆盖合成 RGB/BGR0 到 GPU 无增强输出，不覆盖实卡转换或最终窗口；未新执行 SR/NR/FG、完整 delivery 或独立审查。证据 baseline-result.json、baseline-probe.log、audit-manifest.json。

gh release view v0.0.1 -R Likely7/Veyra-NRVideo --json url,isDraft,isPrerelease,publishedAt 确认已公开发布，时间 2026-09-09T11:00:36Z。纠正旧“尚未发布”记录，不修改远端。保留开工前文档改动，生产代码和运行时未修改，AGENTS/CONTROL_HASHES 未修改，不自我放行历史 hash 漂移。用户授权兼容 OSS 模块复用记录在新方案 R6，保留版权/源码义务与 NVIDIA 二进制边界。

Phase 7 仍 in_progress，产品修复待执行；下一条任务 R1：对原生采集、转换后、GPU 输出和最终显示做同帧定位，再按坏样本修 R2/R3。单次测试 <=300 秒，导出完整性检查保持现状。

## 2026-09-09 原生 YUY2 与实时呈现修复交付

用户撤回实卡 A/B（“不用测试这个，可以确定就是YUY2的转换”），本轮未打开采集设备。实际实现见 docs/BASELINE_REPAIR_IMPLEMENTATION_2026-09-09.md：原生 YUY2/NV12 DirectShow 接收、一次 GPU YUY2→linearFP16、颜色元数据/stride/方向校正，同尺寸 blit/copy 消减、两个批次的异步采集呈现、revision 隔离统计与批量文件日志。保存/暂停/重配置/退出仍按 lease/fence 排空。不能说已同帧实测证明全部色带只有一个根因。

Release 构建 final-build4.log exit0；CPU契约26项、worker11项；YUY2 601/709×full/limited、原生4K及RGB范围 GPU 基准最大误差<=1/255，显示误差0。YUY2八帧NR含7次NVOF/7帧motion；实际Engine文件回放live worker的DLSS2X/4X/暂停恢复/设置/退出10项PASS，FRUC已知15fps平移素材同样10项PASS。没有占实卡。单项均外部timeout290秒。

delivery最终 logs/delivery/37bd4fedc5604642a2a2afeeca46c99e/result.json，23项PASS、42.569秒；NR/NVOF/GBV零错误、4K播放、4K图像、NVENC H264/HEVC音频和2X CFR、取消检查通过。第一次delivery所有功能检查通过但最后Get-FileHash模块未加载而exit1，保留2d62b4ad日志，仅包装导入平台Utility模块后重跑，未改原gate。早期编译头文件遗漏、temporal测试未开启NVOF配置的失败与修正也保留。

独立review_native_capture最终限定复核：所提动态格式/RGB范围/异步统计问题均关闭，审查范围无其他未修P0/P1/P2；它核对源码和真实日志，没有自行执行GPU或实卡，不记整体Phase7 PASS。

另发现旧路径迁移导致本机FRUC运行目录缺NvOFFRUC.dll及其直接依赖cudart64_110.dll。由已有本地SDK复制固定SHA/有效NVIDIA签名的两项到gitignored runtime_local，不修改文件内容。打包allowlist添加固定cudart依赖；仅语法检查，未执行打包或发布。实际FRUC平移回放通过；通用test_av素材返回repeated=true的失败保留，不当有效生成。已发布旧包未更新。

EXE SHA256: 61618AF18F7B985BD4C1FECA06EBE7A07C30EAA01B2CC0682A217E89EAD3C372。原版NR固定SHA一致。已有控制面漂移保留，不改AGENTS/README/CONTROL_HASHES、不声称preflight通过。整体Phase7仍in_progress；下一条任务是用户实测本机新YUY2路径，再按同尺寸同配置证据继续性能优化。FRUC互通/重建等待、GPU临界区、自然运动效果和长期稳定性仍有边界。未push、未重新发布Release。

## 2026-09-09 原生 NR 性能再反馈

完整记录、文件清单、实际命令见 `docs/NATIVE_NR_PERFORMANCE_2026-09-09.md`。完成采集设置fresh帧/arrival锚点、暂停等待、driver断点跨mailbox保留与DeadlineWait复用。最终构建`nrperf-final-build.log` exit0；CPU22、live DLSS12/FRUC12通过；delivery `d3b2dd62224a4df986941a8a4a1cb8a6` 23项45.671秒通过。EXE `8E1A694229257A66DA5F36DE439423632631B310E81258EA032C92CB5FFA159D`。

FRUC候选bSkipWarp/延后重建/时间归零/首对预热均未通过新增reset后像素检查，生产改动撤回，失败证据和候选diff保留。原FRUC新增`--reset-pixels`同样exit1：API成功而部分重复/偏移，不能称原实现通过更严格验收；重建循环未解决。既有短测通过不覆盖这一失败。

本机原生1440p NR额外执行90帧/89NVOF，88条完成GPU timestamp：中位9.74784ms、P95 10.11734ms；Magpie既有同尺寸窗口平均9.718–10.095ms。素材/全链路不完全匹配，只说明纯NR同尺寸未显示倍数差距。用户补充顺序可拖动且实测差异不大，已撤回“顺序是主因”的推断；不变更产品路线。下一任务是同尺寸、同倍率完整链路CPU等待/GPU/呈现节奏诊断。未占实卡、未修改runtime/控制面、未push发布。Phase7仍in_progress。

## 2026-09-10 实时调度修复计划（仅文档）

用户询问截图所述“前沿同步/有限队列与 Depth Anything FP16”是否是当前问题。源码核对确认：采集邮箱为1、presentation active+queued batch上限为2、command ring固定6，因此不存在无界GPU堆帧；但当前缺少完整deadline-aware提交，生成帧可能已经完成GPU计算后才因过期跳过呈现。Depth Anything/TensorRT/ONNX/DirectML目前均未接入；NR使用`nrZeroDepth_`，所以深度推理不是本机高负载根因。

新增 `docs/SCHEDULER_REPAIR_PLAN_2026-09-10.md`：先按revision/epoch建立帧流账本和批量诊断，再仅为实时采集FG加入pre-evaluate deadline skip，随后以有界状态机处理source/graph/fence/present，最后只合入有同源A/B收益的提交、日志或FRUC改动。明确保留mailbox=1、资源lease/fence和文件播放完整性；不把扩大队列、降低质量或原生4K伪装成实时优化。此轮未改产品代码，未运行新的构建/GPU/实卡测试，未更改runtime、控制面、远端或发布；整体Phase7仍in_progress。

## 2026-09-11 README 演示视频

用户提供 `REAMDE MP4.mp4` 作为项目 README 开头的演示视频。文件为 28,200,658 bytes，H.264 1920x1080 60fps、AAC、14.048 秒；未涉及 NVIDIA SDK/runtime、抓帧或测试输入。由于 GitHub README 不保证仓库 MP4 的 HTML5 `<video>` 标签会渲染，使用 ffmpeg 生成 `assets/readme-demo.gif`（480x270、10fps、14秒、约 7.5 MB），中英文 README 均在顶部直接展示自动循环 GIF，并链接到原始 MP4。未创建新 Release；本轮未运行产品构建或 GPU/实卡测试。

## 2026-09-11 软件 NR/SR/FG 音画同步修复

按用户纠正，本次只处理软件增加的视频等待，不额外补偿采集卡音视频共同硬件延迟。实施、文件清单、全部命令及失败证据见 `docs/SOFTWARE_AV_SYNC_REPAIR_2026-09-11.md`。文件音频预填充后等首帧 Present；音频线程按可呈现 PTS 自行停止设备时钟，替代视频线程等 GPU 完成后才以 80/20ms 追赶的旧逻辑。设置重建/seek/暂停恢复保留等待状态；文件普通播放不靠丢源帧或跳过 PCM 追赶。采集重建/暂停失效旧视频锚点，自动补偿上限 250→1500ms、原始+转换中+PCM 总预算 2000ms；手动范围不变。

实际构建 `scripts/build.ps1 -Root <root> -Preset x64-release`，最终 `logs/software-av-sync-20260911/build4.log` exit0。`run-short-test.ps1` 包装运行：audio_timeline 68 PASS（60s超时）、capture_audio 16 PASS（60s超时，80/160/400/900ms 软件延迟、共同900ms输入偏移、重建/断流/边界）、file-endpoint 7 PASS（60s超时）、capture-endpoint PASS（30s超时）。实际 RTX5070 超分4K+原生NR+FG4 文件 Engine 测试10 PASS（180s超时），Present 时最大音频领先26.667ms，包含FG4→2重建与播放/暂停seek；该项为build3，最终端点时钟回退修正后由音频68项、endpoint及delivery覆盖。Feature18/DLSSG Create/Evaluate `0x1`、SEH0；证据 `file-overload/engine.log`。

最终 delivery `logs/delivery/ab1cf0c67a174cc3845c0eb1336c1bae/result.json`：23 PASS、48.892s，包含实际NR/NVOF、4K播放/图像和NVENC H264/HEVC音轨/帧数/时间戳/取消。最终EXE SHA256 `CD4E5EDA37CEACE83C09D327E123A160A0AE814E84729C5D18BD9D4FA68548EE`；根目录 `Veyra.cmd` 指向该本机构建。未改导出完整性gate。

失败记录保留：两次新增音频检查exit1，真实停在120ms并在350ms后保持120ms，最初断言`<120`不含边界；同时修正等待标记在Stop前发布的时序。最终按22ms端点采用覆盖范围后30ms内、不持续增长的验收标准。一次空参数测试包装失败、一次测试运行期间重链接LNK1104；后续顺序构建及检查通过。未执行物理采集/屏幕扬声器对照、长期漂移或XeSS内部延迟验证；不宣称零物理偏差或解决GPU吞吐不足。源码改动均为自有代码/测试/文档，无SDK/DLL/模型变更，无push或Release。下一项为用户本机文件与实卡验收。

## 2026-09-11 音频连续性二次修复（处理30/35ms正常波动）

用户验收指出第一版仍会把正常NR波动变成声音卡顿。核对最新实际会话：4K30文件、XeSS/DLSS；上一版XeSS每33.3ms真实帧却只给音频16.7ms许可，而且任何越界立即Stop。新增 `AudioVideoContinuity.h`，音频owner保留20ms死区、持续100ms或硬领先80ms才重缓冲；显式首帧/seek/重建/暂停仍保持。引擎修正XeSS及对比模式的源帧覆盖，不按不可见子帧停声音。没有新增长期队列或固定延迟，不处理采集卡共同硬件延迟。具体文件、完整命令及证据见 `docs/SOFTWARE_AV_SYNC_REPAIR_2026-09-11.md` 末节。

`scripts/build.ps1 -Root <root> -Preset x64-release` 最终build3 exit0。`run-short-test.ps1`包装：真实PCM/WASAPI抖动before exit1，2X/4X都出现额外停表与速度损失；after 15 PASS、0额外停表/0underrun。完整audio-full 68 PASS。采集30/35ms交替5秒，稳定段额外reset/underrun/missing均0、P95偏差24.997ms；因此本轮不改采集生产算法。日志统一 `logs/audio-jitter-20260911/`。

实际原生1080及原生4K NR＋XeSS分别6 PASS：原生4K source/base/nr/flow=3840×2160，100真实帧前进3.33333秒/耗时3.33008秒，音频额外等待0，末次软件偏差0.896ms；真实Feature18 Create `0x1`/SEH0、XeSS Init/PresentStatus `0`且有生成帧。第一份1080测试因外部GPU争用失败（测试退出GPU仍90–93%、Magpie运行），用户停用增强后开测GPU5%，重测上述两项通过；失败记录没有删除。原生4K SR/NR/DLSS4故意过载12项通过，最大观测领先96.667ms；接受连续性容差后旧“仍过载的重建后<35ms”断言调整为有界检查，并新增性能恢复后独立<35ms检查，防止仅放宽断言掩盖固定偏移。

最终delivery `logs/delivery/c2d5b549af744590b66188aff1dd7cbc/result.json`：23 PASS、45.421s，EXE SHA256 `93F9C4D7D596D833B7E3E347FBC225D77626810F42638E9B57BE88FBED949AC1`。所有单次测试有30/60/180/300s外部上限，导出gate未改。`git diff --check`通过，无SDK/DLL/模型/媒体进入源码变更，无push或发布。实卡听感/声学扫描、长期连续性仍未执行；本轮保证的是被测小波动下不中断音频，不承诺GPU持续过载也能无限保持一倍速与同步。用户重开根目录Veyra.cmd使用本机构建继续验收。


## 2026-09-11 0.0.4 发布候选

用户要求更新 GitHub 并指定 0.0.4。收录两轮软件音画同步及音频连续性修复；中英文 README、构建说明、版本日志、组件说明和 AGENTS 发布授权已同步。完整命令、文件身份、候选资产 SHA256 与验证边界见 `docs/RELEASE_0.0.4_EXECUTION.md`。保留 README 顶部自动播放演示与 WGC 捕获教程。

全新 `scripts/build.ps1 -Preset x64-release -BuildDirectory out/build/release-0.0.4` 构建174目标exit0，正式EXE版本0.0.4、SHA256 `455B17D533D837A88B1A9D8BC27F452677A7D1010033E91EB9B37BF6353DFD9E`。`package-portable.ps1 -Version 0.0.4`沿用七个既有运行文件，发布者身份/签名检查通过，社区版明确HashMismatch；程序仍无manifest加载锁。新解压目录51个清单条目逐文件核验通过，总52文件、forbiddenFiles=0。FFmpeg源码资料10442条，SPDX及实际DLL的LGPL配置匹配。运行组件/SDK/模型不进入源码Git。

`portable-smoke.ps1`清洁PATH、无manifest、包外工作目录的五组检查通过，日志`logs/release-0.0.4/portable-final/result.json`；基础、双NR、DLSS/VideoSR/FG均真实执行，三组分别224/222/227个生成帧。首次测试在最后写报告时Get-FileHash模块无法加载而exit1；显式导入执行宿主Utility后完整重跑成功，保留失败日志，不改产品或断言。先前XeSS各6项PASS被误记7项的文档计数已依stdout纠正。

最终解压EXE的delivery `logs/delivery/f0ab3a10a1ea44e99f8b20e619fbdbdc/result.json`，23 PASS、44.899秒；包含真实NR/NVOF、4K播放/图像/NVENC双编码音轨和完整性。独立发布构建音频完整回归68 PASS、原生4K30 NR＋XeSS连续性6 PASS（额外音频暂停0；3.33333秒媒体/3.33291秒墙钟）、合成采集30/35ms抖动回归exit0。所有单次超时30/60/240/300秒，日志`logs/release-0.0.4/`。未新增实卡声学同步、RTX40或长期直播验证。下一步为原子推送源码/标签、上传并核对四个Release附件后公开发布。
## 2026-09-11 0.0.4 已发布

源码 `cecf34e1f88ea3538f650ef38e46e26c56ef469d` 和注解标签 `v0.0.4` 已原子推送至 `Likely7/Veyra-NRVideo`。Release ID386843250，2026-09-11T07:09:56Z公开，latest=v0.0.4；四个附件远端state/size/SHA256与本机全部一致，Release正文与版本文档一致。发布页 https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.4 。完整命令/身份/测试/失败/未执行项见 `docs/RELEASE_0.0.4_EXECUTION.md`，远端核验日志 `logs/release-0.0.4/github-release-published.json`。源码检查24个文本/自有源文件，无SDK/DLL/模型新增；旧origin与先前Release未修改。

上传期间用户在GitHub提交README修改 `51eb18d`，已快进同步保留，不覆盖、不移动v0.0.4标签或重建资产。仅追加本发布记录。当前发布任务完成，后续为用户实际播放/采集验收；物理声画测量、长期稳定性和RTX40实机仍未执行。
## 2026-09-11 RTX4060持续欠速音频反馈（诊断）

用户提供桌面veyra-app.log，反馈60fps开2X不足120时音频卡顿。只读日志+源码核对：主会话是60fps文件，revision11连续32个计数差窗口源推进均值51.45fps/呈现提交102.89fps；revision8不开FG也仅47.39fps。AudioVideoContinuity在持续领先时仍暂停WASAPI，因此0.0.4只覆盖短暂抖动，并未解决持续欠速下的连续音频。普通日志无逐次音频等待事件，不声称已核实暂停次数或4060声学复现。完整事实、统计口径、策略约束与后续验收见 `docs/RTX4060_AUDIO_UNDERRATE_2026-09-11.md`。命令：rg筛选source/settings/player-timing/audio；Python按同revision相邻时间戳计算processed/displaySubmits速率；读取AudioVideoContinuity/WasapiAudioSink/EngineController。原始日志仅复制到忽略的logs/4060-audio-20260911，无产品/测试/运行时修改，无新构建或GPU测试，无push/Release。下一步是实时播放欠速策略与可观察音频事件，不能继续仅扩大音频等待容差。
## 2026-09-11 连续音频与实时视频调度修复方案（未施工）

按用户要求写成 `docs/REALTIME_AV_SCHEDULING_REPAIR_PLAN_2026-09-11.md`，并为4060诊断及上一版音画同步记录补继续修复入口。方案区分文件提前处理/音频主时钟与采集输入映射/软件延迟补偿；先减少FG提交，原帧处理不足再按PTS分散跳过预览增强，导出完整性保持。明确旧“播放器不得丢源帧”的规则仅拟为实时预览修订，实施时同步，当前未修改AGENTS或产品。

源码核对确认现有FgAdmission是整对布尔准入，当前仅采集使用；XeSS生成属于交换链，其SetEnabled路径需独立验证；MediaFileSource返回借用AVFrame，跨线程候选必须有界持有引用。方案覆盖软历史reset/FG恢复成本、解码参考帧不可乱丢、启动负音频PTS、生命周期、音频事件日志及新欠速验收，P1音频解耦不能脱离P2视频追赶单独交付。量化门槛为待验证目标，未写成实测通过。

实际只运行git status、rg及Get-Content进行文档/源码核对，随后文档链接及git diff --check检查。本轮未构建，未执行RTX runtime、4060/5070或采集实卡测试，未改SDK/DLL/模型，无commit/push/Release。下一条任务P0：明确实时预览契约、补音频事件和0.0.4持续欠速失败回归，再执行P1/P2闭环。
## 2026-09-11 连续音频与实时视频调度修复（隔离分支施工完成）

用户指令接收项目并执行 `docs/REALTIME_AV_SCHEDULING_REPAIR_PLAN_2026-09-11.md`，要求先建存档点、隔离区修复。存档点 `21ce5d3`（tag `checkpoint/av-scheduling-2026-09-11`，与 0.0.4 产品提交 `cecf34e` 无 C++ 差异，`git diff cecf34e 21ce5d3 -- src include apps tests CMakeLists.txt cmake` 为空）；全部修复在隔离分支 `agent/av-scheduling-repair` 提交 `9885759`（P0–P3）与 `5bc91cd`（P4 断言修正），`agent/veyra-v1-loop` 停在存档点，未 push。

实施：P0—AGENTS.md 增补实时预览跳帧契约（仅预览、导出/图片/暂停单帧不跳、PTS 保持真实）；删除 `AudioVideoContinuity.h`；`WasapiAudioSink` 新增 `audio-continuity` hold/release/每秒 summary（clock/coverage/lead）事件。P1—音频 owner 稳态仅显式 hold（open/seek/设置重建/暂停）可停 WASAPI，普通欠速永不停音。P2—新增 `include/veyra/engine/RealtimePreviewScheduling.h` 纯策略（`previewCandidateExpired`/`previewGeneratedExpired`/`XessGenerationGate`）+ `tests/unit/RealtimePreviewSchedulingTests.cpp`；EngineController 文件路径按主时钟流式丢弃过期解码候选（解码顺序与参考帧完整性保持、`previewSkippedBeforeGraph` 计数、软历史断点不重开指标窗口也不伪造 reset 生命周期记录）；文件 DLSS FG 对级准入（主时钟 PTS 截止 + 提交→就绪完成 P95 预测，复用 `admitLiveFg`，样本仅按设置 revision 分界）；完成但过期的生成帧跳过呈现并计入“算完未显示”。P3—XeSS 走 `xefgSwapChainSetEnabled` 迟滞门控（12 帧持续迟到关、60 帧健康恢复，恢复经 presenter 历史重置）；采集生产算法按方案 §3.2 未改动。P4—PlayerSnapshot/实时状态面板新增预览跳帧、播放速度、XeSS 抑制状态，`player-timing` 每秒日志新增 `previewSkipped`/`playbackSpeed`。

构建环境故障与根因：本会话新建 CMake 目录 `CMakeFiles/rules.ninja` 缺失致 ninja 解析失败（checkpoint-build*.log、build1/2/3.log）。逐层定位：MSYS 会话 `TMP/TEMP=/tmp` 与控制台代码页 936 下，CMakeLists include-probe 将 cl.exe `/utf-8` 输出按 GBK 捕获成乱码 `CMAKE_CL_SHOWINCLUDES_PREFIX`（探针复现 `注锟斤拷:...`），Ninja 生成器随后不写 rules.ninja；`chcp 65001` + Windows TEMP 后探针与全量构建均复现修复（build4 起全绿，最小探针项目在 logs 外临时目录验证后删除）。产品构建脚本未改；旧 `out/build/release-0.0.4`（与存档点源码一致）用作 0.0.4 基线二进制。

测试（单次外部上限≤290s，日志均在 `logs/av-scheduling-20260911/`）：单元 `veyra_realtime_preview_tests` 0 失败（30→15 采样间隙=2×帧间隔且覆盖整段、60fps 51/60 欠速延迟有界、极端过载仍持续覆盖、XeSS 门控迟滞/防抖/复位）。A/B—将 0.0.4 版 `WasapiAudioSink.*`+`AudioVideoContinuity.h` 临时检入当前树重编后跑新断言：`underrate-before` 两条稳定失败（600ms 停顿不能一倍速连续、标记 rebuffering）；旧二进制完整套件顺带证实 0.0.4 `stallStartMs=200 stallEndMs=200` 停音。修复后：`underrate-dedicated` PASS（51/60 场景 wallMs=4016 audioMs=3998、额外停音 0、underrun 0）；audio_timeline 完整 68 PASS；`--jitter` 1×/2×/4× 全 0 停音。采集 `veyra_capture_audio_tests` 完整套件首跑 mode=2 meanSkewError=28.2995 边缘超阈（采集代码未改，负载敏感），重跑全过；`--jitter` additionalResets/Underruns=0、P95 20.9137ms。

引擎（RTX 5070 实跑，GPU 空闲 3%）：`--file-continuity`（4K30 XeSS+原生NR）6 PASS，100 帧媒体 3.33333s/墙钟 3.32834s、audioWaits=0、lateMs=0.90ms；`--file-endpoint` 7 PASS；`--file-overload` 17 PASS——持续过载稳态 addedWaits=0、延迟有界（max 47.4ms，窗口末 36.0ms 不增长）、FG 准入 fgSkipped=462/evaluated=12、previewSkipped=173、真实呈现最大稳态间隙 50ms（60fps 源）、playbackSpeed=1.002、FG4→2 重建/播放中 seek/暂停 seek/恢复、退出过载后 lateMs<35ms；`--overload` 采集回放 PASS（skipped=450、command/presentation 上界保持）。delivery gate `logs/delivery/a443858a84cd46539d87f8d41ced4c14/result.json` 23 PASS 46.1s，EXE SHA256 `85BE58B071052B4D015534488B4EB1DACE583FF4A47C300FBC9CDA9FA324B358`，NVENC H264/HEVC 帧数/音轨/时间戳/取消原样通过，`git diff --check` 通过。

失败与修正保留：file-overload 首跑把 seek(.3→.5) 前跳与暂停窗口计入稳态间隙（maxSteadyGap=200ms 撞界），改为向后跳变后首个前向间隙按显式重同步处理后通过；A/B 期间误用 `git checkout --` 还原未提交的 WasapiAudioSink 修改（下一构建报缺 AudioVideoContinuity.h 暴露），重做修改并立即提交隔离分支；无产品级回退。

未执行/边界：4060 实机复验（用户以相同 60fps 文件与设置复测，日志需能独立说明真实处理速率/媒体速度/停音与跳帧原因）、物理扬声器/屏幕同步测量、长时间直播稳定性、真实采集卡 XeSS 欠速门控触发（本轮仅文件路径与单元验证；30fps 非欠速场景门控正确不动作）。不因本机 5070 通过宣称 4060 通过；不承诺持续 30→15 下 NR/FG 时序画质不变。下一条：用户实机验收与 4060 日志复核。

## 2026-09-11 PS5 Remote Play 集成交接

用户要求把 `C:\Users\123\Desktop\Veyra_RemotePlay_Code_01` 中基于 chiaki-ng 的代码接入 Veyra，并在当前进度上整理给下一位 Agent。当前隔离分支为 `agent/remoteplay-integration`，基线 `0f78cc29f34365589bcd4757e7017236e3ac9cb1`，开工标签 `checkpoint/remoteplay-preintegration-2026-09-11`。完整架构、工作树、依赖、缺陷、施工顺序、命令、验收矩阵和许可证边界已整理到 `docs/REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md`，`docs/remoteplay/NEXT_AGENT.md` 已改为唯一入口跳转。

固定 chiaki-ng 提交 `0e16950165f06e5c3291537c2eeba6e852be7120` 已在 Windows x64/MSVC 下完成 248/248 个真实构建步骤；原生 probe 退出码 0，输出 `REAL_CHIAKI_CORE_INITIALIZED upstream_video_callback=1`，并明确输出 `PS5_CONNECTION_NOT_TESTED VIDEO_DECODE_NOT_TESTED WINDOWS_PLAYER_NOT_TESTED`。Remote Play 离线核心测试 67/67 PASS；现有 `veyra_source_tests.exe` 23 checks、0 failures。上述证据只覆盖协议桥基础和原生初始化，不覆盖 PS5 连接、真码流、音频、手柄、增强、OBS、窗口行为或实际延迟。

当前 `RemotePlaySource` 在接入主程序前有七类必修问题：PCM block 被截断后尾部丢失、音视频 PTS 零点不一致、decoder 重建泄漏旧 AVFrame、首帧未进入 Streaming、IDR 请求未转发、decoder delay/多帧输出会错配输入 PTS、非 48 kHz Opus 与固定 48 kHz AudioRenderer 契约冲突；新增 worker 后还要维持严格停止顺序。`EngineController`、`AppShell`、Remote Play 专用音频 owner、DPAPI profile、discovery/wakeup 和手柄设备服务均未接入。下一条唯一任务是先修这些源层正确性问题及测试，再处理生产 CMake 和主程序接线。

外部依赖位于 `C:\veyra-deps\chiaki-source`、忽略的 `out\remoteplay\chiaki-msvc-stage` 和 `C:\veyra-deps\remoteplay-installed\x64-windows-static`，不得提交。chiaki-ng 为 `AGPL-3.0-only` 并带 OpenSSL exception；未来发布组合程序必须提供与二进制对应的完整源码、固定上游版本和补丁、构建脚本、许可证及归因。本轮只更新交接文档，没有修改产品代码、SDK/DLL/模型或运行时，也没有 commit、push 或 Release。

## 2026-09-11 Remote Play 移植代码二次审计（不修产品，交其他 Agent）

用户要求从开始移植时重新审查所有代码并更新交接。审查基线仍为 `0f78cc29` / `agent/remoteplay-integration`，没有新增产品提交。逐文件比对用户 Code 01 包（统一换行后）、当前未提交代码、固定 Chiaki 上游、本机 MSVC patch 与 Veyra graph/audio 接口；结论和18条分级事项已加入 `docs/REMOTEPLAY_INTEGRATION_HANDOFF_2026-09-11.md` 第18节，并同步更正其第6/8/13/17节及 `docs/remoteplay/{NEXT_AGENT,NATIVE_GATE,SOURCES_CODE01,WORKLOG_CODE01}.md`。未谎称其他 Reviewer 或特定模型路由已确认。

新增实际故障证据：使用本机 MSVC 编译隔离审计程序，编译的实现是当前未改动的 `RemotePlaySource.cpp`（仅隔离shadow header打开访问控制以直接注入合成inbox），链接真实FFmpeg、Veyra base、既有Chiaki/core库，无网络/PS5/WASAPI/GPU调用。合法720p H.264 SPS/PPS独立送入当前source返回 `-1094995529` / `no frame!`，`read_status=2 frame=0`；同数据合并配置与AU成功解出1帧。PCM供应480帧、先拉100再拉380，实际仅得到100；音频PTS是约1.36e8ms绝对时钟。12包带重排H.264解出10帧（没有EOF drain，不能把另两帧列为丢尾），已输出的10帧全部配错PTS。实际帧已交付而session仍WaitingFirstFrame；请求1920宽但实际解码1280宽时SourceInfo未更新。另复现音频同格式重启首样本归零被拒绝，以及16位帧号回绕时PTS unknown（后者当前callback不提供wire index，是恢复metadata后会暴露的潜伏问题）。

修正原交接：不能逐块用 `audioPts(firstSample,rate,currentArrival)`，会把10ms推进算成20ms，需固定段起始锚点+样本差值；现有EnhanceGraph已支持YUV420P且会读取明确VUI，因此不是所有画面颜色都错，但source的720p BT.601 fallback和metadata缺失必须修。移植中的普通回调替换删掉了原包真实帧号/profile元数据，三个metadata脚本/测试未导入；core文本主体仍与用户包一致。其余事项包括IDR未转发、decode error直接终止而非恢复、decoder重建与AVFrame泄漏、stop失败被吞、48k契约、凭据驻留、生产CMake未闭环、解码与GPU解耦未接。完整触发条件和修复验收见主交接，不把代码桩和未接UI冒充功能完成。

实际命令/日志：`python out/remoteplay/audit-20260911/prepare.py`（生成两段本地合成素材，每个ffmpeg60秒上限）；`cmd.exe /d /c out\remoteplay\audit-20260911\build.cmd` 两次诊断构建成功（`build.log/build2.log`）；`scripts/run-short-test.ps1 -Exe <audit.exe> -Arguments <single prefix,reorder.h264> -TimeoutSeconds 30` 输出 `observations[2].stdout/stderr.log`。观察程序exit0只代表记录完成，其中是失败证据，不计产品PASS。另通过同一wrapper、各30秒上限重跑core67/67、native初始化exit0、既有文件source23/23；日志 `core/native/source.stdout.log`。native输出的 `upstream_video_callback=1` 为固定文字，不是真回调计数。

全新CMake配置复现旧交接参数组：`out/remoteplay/audit-20260911/configure.cmd` 使用vcvars64/UTF-8/Windows TEMP，真实exit1 `Could not find protoc`，证据 `configure-from-handoff.log`；交接现补ProtocPath/PkgConfigPath，但修订命令的全量构建尚未执行。当前stage的差异全文与MSVC patch一致，递归子模块版本匹配；脚本的文件名/reverse-apply校验不能证明完整源码身份，已列待修而未声称当前stage被污染。证据全部位于 `logs/remoteplay-audit-20260911/`，包含原包文本比对和审查文件SHA256清单；诊断源/生成物在忽略的out目录。

本轮未修产品、未改SDK/DLL/模型、未连接PS5/占用采集卡/关闭用户程序，没有完整Veyra/delivery/GPU/实机测试，无commit/push/Release。下一条唯一任务：先建立H.264 config/AU真实source失败回归并修首帧，再依第18节完成源层正确性闭环，之后接主程序；不能只补UI或继续重复native probe来宣称移植完成。

收尾检查：`git diff --check` 无格式错误（仅既有LF/CRLF提示）；6份Markdown围栏/相对文件链接检查0错误；按审查SHA256清单复核产品/构建/测试代码改动列表为空；`git ls-files --others --exclude-standard` 按DLL/LIB/EXE/PDB/压缩包/合成媒体后缀扫描无未忽略二进制。用户原有未提交代码完整保留。

## 2026-09-11 Remote Play 开工与源层首批修复

用户授权继续完成并在大节点创建Git/更新文档。先存档 `bf21bef`（`checkpoint/remoteplay-audited-2026-09-11`），然后修配置/AU首帧、PCM尾部与固定相对锚点、重排PTS映射、Streaming状态、实际尺寸/颜色、decoder frame释放，并补IDR消费、wire展开、48k/Opus样本序号和stop失败状态。详细文件、实际命令、失败及未测项见 `docs/REMOTEPLAY_REPAIR_EXECUTION_2026-09-11.md`。真实FFmpeg source回归显示480/480样本、首帧成功、重排错配10→0；新目录真实Chiaki/core/source构建通过，core67/67、native初始化exit0，单次测试30秒上限。首建遇第三方头/WX失败，标SYSTEM后通过；保留日志 `logs/remoteplay-audit-20260911/native-source-build*.log`、`source-fixed/core-fixed/native-fixed.stdout.log`。尚未接主程序/PS5/音频设备/GPU；其余审计项和UI/手柄等继续，不声明整体完成。源码/SDK/媒体分离，未push发布。


## 2026-09-11 Remote Play 目标模式节点二施工

用户授权继续至可执行交付，重大节点本地Git存档，实机PS5由用户验收；本轮没有push/release授权。已恢复metadata、严格依赖验证，共享生产CMake、独立网络/解码owner及PCM/WASAPI、DPAPI、配对连接UI和SDL手柄输入。源码编译/正式完整产品build成功；H264/H265/回绕、PCM/PTS、DPAPI、原source23与UI384组合回归通过。当前最终UI、OFF build、delivery及最新mailbox测试继续进行，不能据此称PS5完成实测。详情与真实失败修复记录见 `docs/REMOTEPLAY_REPAIR_EXECUTION_2026-09-11.md` 节点二。


## 2026-09-11 Remote Play 本机测试版收口

节点 `9e5c034` 已存档，追加连接状态/断开、码率与多主机配对管理、中文实机教程和中英文README开发分支说明。ON/OFF正式构建通过；69项native/core/DPAPI、真H264/H265 source/回绕/PCM/PTS、decoded mailbox与SDL边界、原source23项、UI384组合和实际PS5面板本地操作均通过。完整delivery两次PASS（47.53/46.76秒），后续只调整PS5面板并重新实测UI；证据与exe哈希边界详见修复执行记录最终节。用户尚未连接PS5，下一步由用户验收真实串流；没有宣称真实音画同步、网络恢复或手柄硬件已通过。创建桌面本机测试快捷方式，无远端发布。

最终源层加FFmpeg解码分配上限（允许1080p的1088编码填充行）与open错误码，重新构建产品/source并运行source-final-bounded、boundary-bounded均exit0；最终exe SHA256：E66D01B3E5060EAAB508F35E4DE16FDBF1A08CE179290121EDAEF30B43C41203。实机连接仍交用户验证。

## 2026-09-12 主机发现修复
补齐IPv4网卡定向广播、6秒可取消搜索、错误分类与受限日志；构建、边界/UI通过。真实搜索找到开机PS5 192.168.6.232（hosts=1 error=0），未执行配对/串流。首次链接被运行中的exe占用，正常关闭后成功。命令和证据见 docs/REMOTEPLAY_DISCOVERY_REPAIR_2026-09-12.md。

## 2026-09-12 PS5颜色/UI/完整手柄排查与计划
用户确认基本串流、USB和已测增强组合通过；新增发灰、专业状态/模式重绘及全屏手柄故障，要求完整gyro/触摸板与效果，不急发布。静态检查确认AppShell动画/手柄共用timer2及endTransition误停输入；PS5输入FPS仍读captureStats。颜色日志Limited/BT709显式信令，尚未确定发灰根因；核查BT709逆曲线→sRGB显示及范围/alpha链路。ControllerInput/Backend未接gyro/触点/反馈。详见 docs/REMOTEPLAY_COLOR_UI_CONTROLLER_REPAIR_PLAN_2026-09-12.md。仅文档，无产品修改/新实机测试/发布。

## 2026-09-12 目标模式施工节点一

用户已授权施工。timer/真实FPS、PS5显示曲线、sensor/touch/反馈与仅观看初步实现已构建，ON/OFF、69项native、SDL虚拟输入、20+20实际UI切换、GPU灰阶色块及完整delivery47.45秒通过。命令、真实失败、日志、哈希与剩余问题见 docs/REMOTEPLAY_REPAIR_PROGRESS_2026-09-12.md。完整实机、校准/事件与设备路由仍在继续，目标未完成，无发布。

## 2026-09-12 PS5修复收尾与用户验收

补齐SDL触摸事件/传感器批次、120样本静止校准、16项/100ms跨线程输入队列、失焦立即释放、能力状态与音频子系统引用计数修复。用户反馈“可以了，我测试了没问题”；未逐项覆盖的蓝牙/多设备/主机直连账号共存等如实保留。最终校准超时起点修正另经自动测试。

build-product/native-source/off-check成功；boundary默认与virtual、H264/H265 source成功，CTest69/69（0.50s），实际20+20 UI切换通过。运行中的EXE导致LNK1104，正常关闭后重建成功。完整命令、日志、SHA256、用户验收与自动验证边界见 docs/REMOTEPLAY_REPAIR_PROGRESS_2026-09-12.md 节点二。本地Git存档，不push/release；源码无SDK/DLL/模型/凭据。

## 2026-09-12 PS5遥测、停帧与补帧降级再排查（仅方案）
用户反馈UI可操作但画面停帧、数据面板混乱、30→2X未达目标后回到原帧率，询问软硬解切换。只读核对30694da源码及既有音画调度计划，发现LiveStatusPanel仍读采集FPS、平均/P95标签不明、GPU完成与呈现混淆、累计预算标志常驻；日志一段95张有效FG仅19呈现、76过期，95次warmup。冻结根因和30→60确切场景未复现。日志保存logs/ps5-telemetry-audit-20260912（忽略），完整证据与P0-P3方案见 docs/PS5_TELEMETRY_STALL_FG_DECODE_REPAIR_PLAN_2026-09-12.md。当前只新增文档，无产品修改/构建/新GPU测试/发布。方案初次apply_patch因WORKLOG上下文不匹配未写入，随后重新写入并检查。

### 同日追加：用户复现4K30原生NR＋2X锁原帧率
新日志revision4确认NR/flow/FG均4K，690次FG候选中682次拒绝、8次Evaluate（1预热＋7有效），7有效全部过期，generatedPresented=0；原帧约30fps且媒体1×。文件一批处理/等待呈现完成后才处理下一批，与插帧中点早于B原帧截止时间的差异形成强疑点。方案P1增加有界提前增强/呈现解耦，不能只调整FG阈值。日志与SHA256见方案补充节；这是用户复现加日志/静态审查，不是Agent新执行的负载测试，产品尚未修改。

## 2026-09-12 全部修复目标开工：P0独立进度
开工adff31b，分支codex/ps5-scheduler-telemetry-decode。独立inbox接收/解码窗口、GPU/Present进度、受限关键帧恢复已构建和窄测通过；故障注入尚未验证，其他P1-P3继续。详见docs/PS5_TELEMETRY_SCHEDULER_EXECUTION_2026-09-12.md。目标active，无发布。

### 2026-09-12 文件FG提前增强节点
按PS5_TELEMETRY_STALL_FG_DECODE_REPAIR_PLAN实施文件预览容量2的提前增强。实际4K30原生NR+DLSS2X测试和暂停seek回归均退出0，短媒体稳态约60呈现提交/秒，生成过期0，音频约1倍速。详细命令、日志和未验证边界见PS5_TELEMETRY_SCHEDULER_EXECUTION_2026-09-12.md。仍未完成全部修复，不发布。

### 2026-09-12 PS5硬解/遥测与欠速恢复节点
实现PS5自动/软件/硬解选择、实际D3D12VA纹理输出与GPU消费引用保留，自动失败回退及强制硬解报错；重做实时状态分组与PS5独立接收/解码/呈现统计。实现FG分成本预算、限频连续恢复探测，移除旧同步文件分支。详见PS5_TELEMETRY_SCHEDULER_EXECUTION_2026-09-12.md。
产品ON/OFF和native source构建通过。真实H264/H265软硬对比max_error=0，故障回退注入通过；NR/SR/FG欠速、动态负载恢复、XeSS连续音频、输入中断与EOF回归通过。45秒4K30原生NR2X：1286源帧、1253生成，absLatenessP95=0.79ms，非所有帧必达目标的承诺。统一gate、最后UI验证和最终用户说明待完成；不发布。用户要求修复结束后正常关机，明天实测PS5。

## 2026-09-12 最终本机交付节点

上述待办已经执行：最终 product 构建通过（logs/ps5-final-product-guard-build.log），Remote Play OFF 构建通过；Native CTest 69/69，文件源23项、调度/呈现/UI/边界回归通过。统一 delivery gate 45.29秒通过，证据 logs/delivery/82d949d48503403495e10de477ce70d8/result.json。最终 EXE SHA256 7AA2452E05E6B423DB1C7A4D3D66B9C9D2BFE41262C337B8D70010EBE301E61A。

45秒4K30原生NR+2X实际长测：1284原帧呈现、1247生成帧呈现、过期5，末段59fps；并非零丢帧或全硬件60fps承诺。400ms源间断恢复后240帧原帧全部呈现、EOF无取消；单帧EOF无挂起；3X、动态欠速恢复、XeSS音频连续性均通过。所有单次测试均小于300秒。软硬H264/H265色值比较最大差0，强制硬解无设备拒绝和自动回退最终回归通过。

UI最终使用真实窗口DC抓取并检查非黑图，已人工查看overview/advanced快照；20次模式切换及20次全屏通过（logs/ps5-final-ui-visible.log）。此前隐藏子窗口PrintWindow黑图仅是无效测试方式，不作为产品通过证据。

新增 docs/PS5_REPAIR_ACCEPTANCE_2026-09-12.md 汇总测试入口、日志与验收边界，中英文README更新开发分支状态。桌面“Veyra PS5 测试版”指向 out/remoteplay/product-repair/veyra.exe，移除smoke/禁用增强启动参数。代码节点3405717。未push、未发布，未提交SDK/DLL/模型/测试媒体。

下一步唯一任务：用户明天实际连接PS5验收新增硬解、负载恢复与偶发停帧。原冻结未复现，原始根因不能断言；本轮没有真实PS5/采集卡复测，不把本地码流测试冒充网络验收。用户授权收尾后正常关机，不使用强制关闭参数。

## 2026-09-12 增强额外延迟估计

用户确认主面板需要相对无增强播放的新增延迟，允许预估。新增 EnhancementDelayEstimate.h；Engine在实际原帧Present后记录一秒窗口样本。文件使用媒体时钟正向lateness（预处理驻留不计，启动/seek重新锚定不属于稳态）；直播使用已解码时间到Present的帧龄，减去颜色、输出合成及Present基础开销估计。PS5取真实decodedHost，采集无该时间戳时取callback，可能包含基础转换/排队而偏高。没有同源同时无增强A/B标定，不承诺精确因果差值；无增强定义0、基线缺失返回未测，负值夹0。XeSS内部排队/屏幕扫描不可测。故不应称端到端实测。

LiveStatusPanel主数改“增强额外延迟 · 估计”，原驻留均值/P95移至详情。本轮不改变音频、增强、调度策略。单独记录估计样本，不能用不同统计群体的P95相减。

构建 out/remoteplay/build-extra-delay.cmd 通过，logs/extra-delay-final-build.log。repair_contract_tests 88 checks 0 failures（含预读取不计延迟、基线扣除、未知/无效样本检查），logs/extra-delay-contract.log。UI首次脚本过早检查WM_CREATE子控件失败，保留logs/extra-delay-ui.log；加同步WM_NULL等待创建处理完毕后重跑通过，logs/extra-delay-ui-retry.log，overview实际截图已查看。最终仅修改底栏文案后重新构建通过。

用户GTAVI_An_Extended_Look_4K_Native.mp4实测12秒 --native --nr --fg-multiplier 2 --no-sr --smoke-seconds 12，exit0、291原帧/289生成、failed=false、末段约60呈现/秒、absLatenessP95=0.92ms，旧驻留P95=67.022ms；证据logs/extra-delay-4k.log。未执行实卡/PS5新对照测量。软件路径仍 out/remoteplay/product-repair/veyra.exe，桌面PS5测试版指向此处。未发布、未push、无二进制入Git。

## 2026-09-12 状态面板卡片与曲线

按用户图片将默认实时状态面板改为深色圆角卡片：光流/NR/SR/FG四项最近一秒GPU均值；30秒额外延迟估计历史（250ms采样，缺失断线、不填0）；旁边总估计；底部待输出帧数和状态。详情保留旧阶段诊断。代码 apps/veyra/ui/LiveStatusDashboard.h。

FrameFlowMetrics.pendingOutputFrames 接实际呈现作业中尚未消费的有效帧机会（含待GPU完成、待截止时间的原帧/有效生成帧，不把两个batch冒充两帧），正常呈现、过期、取消均扣除；XeSS SDK内部队列不可观测，卡片星号注明不含内部队列。低于请求目标95%持续8个250ms样本判黄过载；达到阈值持续8样本恢复绿正常；failed红错误；待机/暂停/采样/调整灰。95%容差防止59.94相对60等正常抖动报警；无目标不据此推断性能。软件reported failed以外未知故障不能凭低FPS武断标红。

构建logs/dashboard-final-build.log通过；中途scope guard初始化/文本替换两次编译错误修复，保留dashboard-build.log和dashboard-build2.log。UI脚本logs/dashboard-ui.log通过（模式切换/全屏/详情），overview截图实际查看布局完整。修复待机applying残留显示后最终构建通过。repair_contract 88checks0failures，logs/dashboard-contract.log。

用户4K视频原生NR+2X实际12秒smoke退出0，294原帧、292生成、failed=false。logs/dashboard-4k.log，稳态pendingFrames=1，额外延迟估计约0.4–0.5ms，absLatenessP95=0.81ms。仅本地RTX运行验证，未做PS5/采集卡实测和人为故障红灯注入。没有发布、push或二进制入Git；桌面PS5测试版仍指向已更新EXE。

### 曲线卡片内切换精细面板

用户指定只在曲线卡片区域切换。右上小三角切换精细数据/曲线；顶部四卡、底部队列与状态固定。精细列表按卡片高度裁切完整行，滚轮仅在卡片内容区生效；返回曲线保留历史。旧全局标题点击切换已取消。

构建 logs/dashboard-inset-final-build.log 通过，UI脚本按DPI点击新位置、依次抓取overview/advanced/returned，logs/dashboard-inset-final-ui.log通过；实际查看advanced截图，顶部/底部固定且文字未溢出卡片。首次链接被运行中EXE占用，正常关闭后重建；测试脚本首轮坐标变量遗漏，补齐后重跑，上述最终结果为有效证据。未修改播放链路，不重复GPU性能测试；未发布。

## 2026-09-12 拖动进度条回弹及输出槽占用错误

用户日志03:09:30.998 frame-pool slot=1 still leased; refusing overwrite batch=1276。此前两次seek约344.93/643.709秒已完成，再播放时发生。原始日志保留logs/seek-user-original.log。证据证明资源仍被占用；不能仅凭这条日志确定唯一引用持有者。

Engine背压从只检查两个batch容量改为同时检查下一奇偶输出槽所有real/generated弱引用是否释放；推进呈现/完成观测后再取下一帧，不覆盖活跃纹理。跳转请求在背压等待中到达时立即返回外层处理seek，避免旧时间线继续取帧；无作业却长期占槽才超时报错，不把正常低帧率deadline等待当错误。未关闭原frame-pool保护，未增加每帧GPU fence阻塞。

进度条松手后原来立即用旧snapshot.position刷新，导致回弹。现在seek请求和呈现确认有序号，发出后保持最新目标，只有该请求对应的新帧实际Present后才恢复跟随；TB_ENDTRACK不重复发请求，时间文字显示目标及跳转中。暂停连续请求以最后一次为准。顺带修复有效生成帧从Pending到Valid时队列计数增加可能触发unsigned减法的问题。

验证：最终构建logs/seek-ui-final-build.log；repair_contract 88checks0failures。新增LivePresentationTests --seek-stress，实际用户4K长视频、原生NR+3X，六次前后seek（含原日志两位置）、每次后续45原帧、暂停连续32/44/61秒seek、恢复和关闭，16项通过exit0，logs/seek-stress-final.stdout.log与logs/seek-stress-final/engine.log，180秒上限内结束。此前首轮测试分支插入遗漏误入旧测试导致FAIL，logs/seek-stress.stdout.log保留；修正后seek-stress2及最终两轮均通过。UI既有切换脚本logs/seek-ui.log通过；未通过自动鼠标视频测试独立逐帧验证拖动视觉，需用户实测手感。未做新的PS5/采集卡测试、未发布或push。

## 2026-09-12 专业设置阅读顺序

按用户要求调整UI，不改变增强执行顺序：NR运行版本→实时/原生NR处理档位→超分开关/目标/方式；运动页先光流提供方、性能选项与质量，再补帧方式与倍率；采集/串流音频同步移入独立音频页。页签为增强、运动、音频、预设、导出，旧控制ID及数据绑定保留。

apps/veyra/SettingsWindow.cpp调整布局；apps/veyra/ui/AppShell.cpp新增音频页签（不移动旧枚举ID）、排列和切换。logs/settings-order-build.log构建通过；临时UI检查脚本out/remoteplay/test-settings-order.ps1基于实际HWND矩形确认218<203<201、209<204<208<202，音频页独立可切换，既有专业/全屏切换脚本通过，logs/settings-order-ui.log。纯UI布局调整，未重跑GPU或实机串流性能测试。未发布或push。

## 2026-09-12 NR先行低延迟与悬停帮助
完成默认关闭的NR→SR→FG实验预览开关、旧预设默认关闭及v11存储，参数/播放/采集/PS5悬停说明。详细代码、测试命令、失败修复与未验证边界见 docs/NR_BEFORE_SR_PREVIEW_2026-09-12.md。90项contract、42组预设迁移、实际两种SR后端+NR+FG与恢复默认9项、统一48.25秒gate及UI通过。无新SDK/运行时，无push/release；实卡及PS5画质由用户验收。

## 2026-09-12 PS5 HDR、PSN与主机保留规划
用户要求先写方案。新增 docs/PS5_HDR_PSN_HOST_PLAN_2026-09-12.md：画质分段定位、实测码率、稳定用户目录及旧配对迁移、PSN浏览器授权/刷新/条件性自动注册、Main10/HDR显示与SDR映射、增强兼容能力矩阵和验收节点。静态检查确认现有配对已DPAPI保存，目录随applicationRoot变化；RemotePlaySource与EnhanceGraph拒绝HDR，不能只增选项。重复配对根因与本次糊灰尚未实测确认；既有BT1886修复不能当作当前无问题的证明。本轮仅文档与代码/官方上游资料核对，无产品修改，无PS5/OAuth/HDR实测，无发布。

## 2026-09-12 PS5 HDR/PSN/主机持久化实施

开工标签checkpoint/ps5-hdr-psn-preimplementation-2026-09-12，方案提交37cfdf4。固定用户目录及DPAPI旧档迁移、稳定主机ID和观看模式、PSN浏览器回调授权/刷新/注销、H265 HDR与Main10输入、HDR原生旁路/SDR映射后增强已经进入产品代码。发现并修复sws_scale目标平面数组只有2项导致新10-bit Full测试访问异常；补齐P010码值、PQ/色域及FP16呈现。详细命令、失败与未完成边界见 docs/PS5_HDR_PSN_EXECUTION_2026-09-12.md。8组HDR GPU/呈现、Main10软硬解各12次真实NR、SDR颜色回归、90项contract及UI通过；42.73秒gate为收尾前二进制，最终按针对性测试报告。真实PS5画质、Sony登录未验收；免PIN自动注册、原生HDR增强不宣称完成。无发布/push/运行时入Git。

收尾HDR组合回归：logs/ps5-hdr-combo.log，软/硬解 × NR单独/标准SR→NR→FG/低延迟NR→SR→FG，共6组通过；组合4K/2X各12次SR、12次NR、11有效生成帧。实际PS5画质与Sony授权仍待用户操作。

## 2026-09-12 悬停说明实际不显示修复
用户反馈悬停没有效果。本轮实际鼠标命中低延迟按钮后验证：旧注册路径 tooltip 可创建但 TTM_GETTOOLCOUNT=0；不是窗口存在就算通过。TOOLINFOW 使用完整 sizeof 在当前 common-controls 环境被拒绝。改为 TTTOOLINFOW_V2_SIZE 后 count=103，实际悬停可见；同时嵌套控件使用直接父窗口和已有静态帮助字符串，避免依赖中间面板转发文字回调。AppShell 与 SettingHelp 共用注册处检查返回值，失败写 ui-help 日志。
修改 apps/veyra/ui/AppShell.cpp、SettingHelp.h。构建命令 cmd /c out/remoteplay/build-extra-delay.cmd，最终 logs/hover-final-build.log 成功。实际鼠标脚本 out/remoteplay/test-hover-real.ps1，logs/hover-visible-final.log：按钮命中、103项注册、提示 visible=True；logs/hover-visible.png 已查看，中文说明完整、深色背景。此前只验 tooltip HWND 的旧 nr-first-help-ui 不能证明悬停功能通过，本条修正该验证缺口。
本轮第一次更换父窗口/文字回调后仍失败，第二次尝试显式 relay 仍失败，均保留失败结果；relay 已撤回，真正恢复发生于 V2 结构体尺寸修复。UI回归首次用 Windows PowerShell 5 读取无BOM中文脚本产生解析错误（logs/hover-final-ui.log），改用 pwsh 重跑（logs/hover-final-ui-retry.log）。无增强/音视频管线修改，本轮未执行新 RTX Create/Evaluate 或实机 PS5 测试。

## 2026-09-12 0.0.5 合并与发布准备
用户授权后，main同步远端README改动并合并PS5开发分支，独立436步构建成功。双语README、Release说明、运行组件/串流许可证与对应源码补齐。外部便携五组、47.046秒统一gate、DPAPI、SDL边界、90项合同及便携PS5 UI通过。最终运行文件与测试哈希一致，71文件白名单通过，未提交SDK/运行时/凭据。资产、命令、日志、真实验收边界见 docs/RELEASE_0.0.5_EXECUTION.md，发布结果待追加。

0.0.5已发布：main和标签源码提交7d8e24c，Release 387470534，公开2026-09-12T05:54:45Z，latest=v0.0.5。六附件远端大小/SHA256与本机一致，README blob一致；证据github-published.json与github-verify.log。无旧版替换、无SDK/运行时/凭据进入Git。发布后仅补本记录，实际PS5/PSN/HDR边界保持不变。

## 2026-09-12 文件过载迟到后续修复方案
用户4060日志显示revision13迟到均值62.649ms、队列2，音频持续推进；revision14另配置迟到0.578ms。静态检查发现源帧过期仅比较当前时钟，未预测增强完成，以及“原帧总呈现”的单批次历史假设与现有容量2不符。新增 FILE_OVERLOAD_LATENCY_REPAIR_PLAN_2026-09-12.md：先澄清文件迟到/实时额外延迟口径，关联测量、预计就绪选帧、有替代结果时跳旧呈现，保持音频连续及导出完整。仅方案与日志/代码审查，未新构建/运行RTX测试/修改产品/发布，不能断言全部60ms可消除。


## 2026-09-12 增强处理耗时主面板与应用图标
用户确认后开工存档7a30b66，分支codex/processing-metrics-and-app-icon。主曲线与大数字改为同帧光流/NR/SR/残差/FG批次GPU区间去重后的处理耗时，排除呈现等待与音频；原有额外显示延迟移到小三角详情第一项。XeSS内部FG缺计时继续明确排除。用户Logo转换为七尺寸ICO，嵌入EXE大/小窗口图标与专业模式品牌位。没有修改音频/跳帧/呈现调度，不能将本次显示修正称为过载迟到已修复。
构建两次成功，命令cmd /c out/remoteplay/build-extra-delay.cmd；98合同检查通过。180秒4K文件NR+DLSSG短测5275帧、5251生成帧、failed=false；最终构建另用1080色条文件到4K测试视频SR+NR+DLSSG，Create result=0x1/SEH=0、FG warm-up Evaluate=0x1，实际GUI主数字约12.1ms、详情首项迟到约0.5ms，曲线/详情切换及Logo可见。ExtractIconExW确认EXE一组图标；具体命令、文件、范围与日志见docs/PROCESSING_METRICS_ICON_2026-09-12.md。未测试实卡、PS5实机及XeSS内部计时，未发布。下一步由用户体验新版面板；过载调度方案仍单独待实施。

## 2026-09-13 PS5 H.264 硬解细条修复
基线83f90ba，分支codex/ps5-decoded-frame-strip。用户软件解码正常；同真实PS5 AU软/硬解比较定位到FFmpeg n9.0.1 H.264 MAX_SLICES=32，而PS5输入68 slices，硬解在进入增强前已经损坏。外置开源源码容量改256并按原LGPL功能配置重编译；同码流全图误差由167.459变为0，已查看完整装备页，日志ps5-live-decode/ps5-live-patched。另用两张黑白图并发GPU排队复现并修复共享硬解SRV槽覆盖，按已有两槽fence轮转并使用staging，修复后4组错误通道归零。
产品构建cmd /c out/remoteplay/build-extra-delay.cmd成功（最后ps5-strip-final-build.log）；普通视频软硬解全图、8组HDR颜色/呈现、6组Main10软硬解NR/SR/FG组合、40秒主程序硬解NR（1135次NR，1134次NVOF，failed=false）、UI合同回归通过。NR CreateFeature18=0x1，SEH=0。未将共享Source/Graph实机同AU验证冒充主UI长时PS5/HDR/手柄/音频验收。
新FFmpeg五DLL更新本机测试目录与默认开发前缀，原件忽略目录备份；新增源码补丁、重编译脚本、双层provenance打包校验及真实全图诊断。对应源码ZIP本地验证通过，未发布；SDK/NVIDIA运行时/依赖DLL/媒体/凭据均未入Git。失败记录包括：诊断缺include、MSYS link遮蔽MSVC、零上下文补丁apply失败，均已修正；最初GPU回读一致但画面仍坏的检查不作通过证明。详情、命令、哈希、日志与后续验收见docs/PS5_HARDWARE_STRIP_REPAIR_2026-09-13.md。用户下一步重开桌面PS5测试版，选自动优先硬解或D3D12VA重连体验。

## 2026-09-13 PS5 DLSS 补帧、过载计时与声音补偿修复
基线 c68b4b0，分支 codex/ps5-fg-overload-audio。日志证实 DLSS 候选大量被截止时间判定跳过、实际串流约 59.9314 fps 而视频时钟固定除以60、decoded mailbox 覆盖误标 Discontinuity 导致统计窗口不断重开。修正本地估计时钟渐进校准、音频 ingress 映射有限老化、Drop 分类与同配置软历史重置的异步计时保留；PS5 每对新解码输入在增强前确定补帧预算，不用增强完成延后截止时间。UI 不再把已开启但等待执行的 FG 写成未开启。
构建 cmd /c out/remoteplay/build-clock-product.cmd 成功；离线核心68/68，合同103/103，调度与RemotePlay边界通过。真实WASAPI基础回归及120秒慢钟差通过，P95音画偏差4.254ms、仅启动reset1次、无溢出。前三轮实机回归失败均保留：逐步定位覆盖事件分类、压缩AU锚点和硬解交付抖动。第四轮120秒PS5 H264硬解+4K视频SR+实时NR+DLSS2X通过；115秒有效生成累计5251，末期有效补帧53fps，过载163/163保留计时，音频等待早期64.587/末期57.892ms。NR/DLSSG Create与预热Evaluate=0x1，SEH=0。不能宣称固定120fps或小时级验收。
证据 logs/ps5-clock-live4-result.log、logs/ps5-clock-live4/engine.log、logs/ps5-clock-audio-drift.log；其余命令、失败边界和修改文件见 docs/PS5_FG_CLOCK_METRICS_REPAIR_2026-09-13.md。运行中的旧EXE重命名保留，最新主程序已构建到原快捷方式路径，用户重开才生效。未改运行DLL，未提交SDK/二进制/凭据/日志，未推送发布。下一步由用户长时游戏体验验收。

## 2026-09-13 PS5 断流自动恢复

基线 b5cb1b5，分支 codex/ps5-stream-recovery。现场日志完整输入/解码停在 7053，队列0、decoder非忙碌，旧三次IDR及30秒等待未恢复。新增1秒/3秒关键帧恢复、6秒后旧会话顺序退出并复用本次内存凭据重连，最多3次且1/2/4秒退避；主动断开可取消。重连首帧重置历史和音频时钟、应用序号连续；界面显示恢复中。增加底层包窗口、回调拒绝、组帧/传输分类与终止码日志，不输出上游原始字符串/密钥。

构建 cmd /c out/remoteplay/build-extra-delay.cmd 成功；cmd /c out/remoteplay/build-clock-tests.cmd 核心73/73；cmd /c out/remoteplay/build-boundary-clock.cmd 后边界通过，合同103/103。实机使用 veyra_live_presentation_tests.exe --last-paired-ps5 <目录> --reconnect：第一轮恢复成功但单点FG性能断言失败；第二轮暴露PS5旧会话短暂RP_IN_USE导致放弃，修成已有恢复预算内继续退避。第三轮50秒PASS：断流前已播放、第二次重连成功、730次恢复后健康采样，NR/SR4K/DLSS2X及WASAPI音频恢复。NR与DLSSG Create=0x1/SEH=0，DLSSG evaluates=1276/Release=0x1。另 --cancel-reconnect PASS，主动停止后idle且不重连。证据 logs/ps5-stream-reconnect3-result.log、对应engine.log及logs/ps5-stream-cancel-result.log；详细失败、命令及修改文件见 docs/PS5_STREAM_RECOVERY_2026-09-13.md。

这是可控断流与自动恢复验证，不是最初自然断流根因已查明，也不等于长时PS5/HDR/手柄触觉验收。没有更换运行DLL、没有提交SDK/配对/日志、未推送发布。下一步用户重开桌面PS5测试版长时游玩，如再断流带新诊断定位上游原因。

## 2026-09-13 PS5 原始码率/画质审计

基线5c3fad0，分支codex/ps5-source-quality。用户允许源头实测，禁用SR/NR/FG：H264 1080p30请求100Mbps，PS5反馈目标97.087Mbps，菜单有效视频均值10.018；H265同请求均值5.956；H264请求15Mbps，主机目标14.563、有效均值5.242。各35秒、硬解、1920×1080，完整帧丢失/回调拒绝/错误均0，保存1:1源图已查看。不是Veyra把100截成15；实际码率为内容/主机决定，不能由未跑满推导画质低，也不能靠菜单证明游戏清晰。已请求用户切换实际游戏场景，游玩模糊尚未定位。

修正PS5面板显示待选值却不提示重连的误导：展示本次真正请求/实收视频码率、明确应用设置并重连、重开面板继续跟踪会话；日志与专业诊断加入请求码率及codec。诊断工具仅显式opt-in读取主机数值品质反馈，正常不启用逐包verbose，无原始上游字符串/密钥输出。构建build-source-quality.cmd与build-extra-delay.cmd成功，合同103/103，diff检查通过。三轮实机命令、PNG、日志路径及待验收边界见docs/PS5_SOURCE_QUALITY_AUDIT_2026-09-13.md。没有改像素算法/运行DLL/用户配对，未执行增强Create/Evaluate、未做UI视觉回归、未推送发布。下一步必须是实际游玩原图与呈现对照，不能宣称画质已修好。

## 2026-09-13 PS5 模糊客观链路审计（取代上一条看图/换场景验收要求）

用户明确装备页人物也模糊，Agent不再凭内容识别判断画质，肉眼验收由用户进行，不以退出菜单为前提。基线082db52，仍在codex/ps5-source-quality。检查协议、RemotePlaySource、ResolutionPlan、颜色契约、真实GPU输出和交换链；未发现100Mbps被限15Mbps、关闭增强偷偷降720p或正常1:1额外低通。确认普通缩放为双线性、色度2×2复制且未携带chroma_location；这是本地可改善差异，尚未证明是严重模糊的全部根因。Sony今年Portal新增1080p高质量码率档，但公告未给协议参数；不据此声称已启用或找到无损/4K接口。

新增SourceFidelityTests及CMake目标，build-source-fidelity.cmd成功；真实RTX5070灰阶/细线2组及1:1/1440p/2160p两交换链呈现12组通过，1:1误差0，灰阶最大误差0.584/255，普通缩放匹配独立CPU双线性参考。颜色既有4组通过；普通H264硬解4帧及并发纹理检查误差0。命令与结果logs/source-fidelity-{build,result,color,hw}.log，单项均不足300秒。新诊断无PS5连接、无主机操作；当时chiaki运行，不争抢会话。没有新NR/SR/FG Create/Evaluate，未改产品算法或运行DLL，未重新宣称PS5 H265/HDR实机通过。当前测试仅到呈现缓冲，不包括DWM或显示器。旧实际PS5同AU误差0是此前证据。

修正旧画质审计中的视觉结论和换场景前提，详见docs/PS5_SOURCE_FIDELITY_AUDIT_2026-09-13.md。下一步是色度位置/普通显示采样的独立数值对照与用户肉眼验收，源头编码质量另查同码流元数据/量化，不能用SR掩盖。未推送发布；SDK、DLL、配对、日志与媒体不入Git。

## 2026-09-13 PS5 精细采样实现（等待用户验收）

用户授权修复后关机，随后撤回音频缓冲选项；未改任何音频逻辑。基线320f860，存档checkpoint/ps5-sampling-2026-09-13，分支codex/ps5-sampling-repair。新增显式色度位置契约与按位置插值（缺失left回退有日志）；PS5普通放大使用带局部范围限幅的Catmull-Rom，像素中心1:1直接读取，缩小保留旧路径。默认精细、保留兼容采样，PS5面板可选且重连生效，选择保存在现有本地settings.ini；不改变主机配对、codec、码率和HDR选择，不另造播放循环/音频/队列或回读。文件、采集、导出默认不启用新采样。

cmd /c out/remoteplay/build-clock-product.cmd 两次成功，日志logs/ps5-sampling-product-build.log与logs/ps5-sampling-final-build.log；DXIL及C++编译链接成功，diff检查通过。按用户要求不运行测试、不连接PS5、不做肉眼判断；NR/SR/FG Create/Evaluate、GPU采样成本、HDR与UI显示均未执行，不能把编译成功说成视觉改善已验收。修改文件/链路边界/明天A-B方法见docs/PS5_SAMPLING_REPAIR_PLAN_2026-09-13.md。桌面测试版对应out/remoteplay/product-repair/veyra.exe。未推送发布，未提交SDK/运行时/凭据/日志/测试媒体；完成本地存档后执行用户授权的正常关机请求。

## 2026-09-13 正式发布前完整审查（发现发布阻断，未修产品代码）

用户要求正式发布前审查 bug。基线 f037f49，codex/ps5-sampling-repair，开工工作区干净；本轮用户要求审查后实际执行测试，不能继续沿用上次“仅编译”的证据限制。报告 `docs/RELEASE_AUDIT_2026-09-13.md` 列出 5 项确认问题：P1 欠速播放到 EOF 复用已清空 AVFrame 导致 0xC0000409；P1 变分辨率文件用新尺寸写旧 YUV 上传容量；P1 损坏尾部导出 52/60 帧仍报成功且音轨缩短；P2 partial 检查/打开竞争可覆盖另一任务文件；P2 内嵌字幕导出无提示丢失。只形成审查结果，未修复这些问题。

执行 `scripts/build.ps1 -Preset x64-release -BuildDirectory out/release-audit-20260913/build -RemotePlay`（完整依赖参数见报告），444 步成功；`scripts/gates/delivery.ps1 -Root . -BuildDirectory out/release-audit-20260913/build` 的 23 项通过，45.033 秒，result=`logs/delivery/c992884d6d924d02a4cce0374a758094/result.json`。临时 runner 的 27 项及扩展 16 项运行通过；初次 seek 压力用 60 秒素材却要求跳到 700 秒导致退出 1，保留原失败，改 710 秒素材后相同测试全部通过（22.156 秒）。Remote Play core 新构建 73/73；精细采样临时数值对照 30 组通过，1:1 最大误差 0、放大 0.560/255、六色度位置 0.588/255，均为合成 SDR 呈现缓冲检查；不是游戏画质或显示器验收。

本机 RTX5070/616.56 实际 NR Feature18、SR、DLSSG Create 均 result=0x1/seh=0；NR/FG/NVOF 实际执行及输出验证见 nr-flow/sr-nr-fg 日志，便携 VSR smoke 记录 nrEvaluated=120、nvofExecuted=117、generated=115。真实 USB3 Video 1080p60 YUY2/MJPEG 各收到 19 帧，消费者停顿时正确丢过期帧；未据此宣称实卡画质/4K60通过。本地 `package-portable.ps1` 与 `acceptance/portable-smoke.ps1` 的 5 场景通过，七文件 manifest 身份/许可证/扫描通过，软件不依赖 publisher manifest 的决定保持有效。审查 ZIP 只在本地，未上传，不能作无缺陷正式候选。

完整命令/日志/失败复现位于 `logs/release-audit-20260913/` 和报告，辅助脚本/合成素材均在忽略目录。原版/社区 NR 身份符合批准记录；patched avcodec SHA256=0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F，未退回未补丁 FFmpeg。未修改、提交任何 SDK/DLL/模型/个人配置/凭据/测试媒体。未连接实际 PS5、未做 PSN/HDR 显示器/长时稳定性/多 GPU/4K60采集端到端验收；本轮没有独立 Reviewer。修改仅为本审查报告与 WORKLOG。下一项唯一任务：修复 EOF 候选帧寿命并针对性回归，再按报告优先级清理其余问题，当前不建议正式发布。

## 2026-09-13 仅修复发布审查三个 P1（本地回归通过）

用户明确“只修复p1”，本轮仅处理 F1/F2/F3，F4/F5 两个 P2 未改。基线 f037f49，存档 tag 为 checkpoint/release-p1-2026-09-13，施工分支 codex/release-p1-repair；保留此前审查报告与工作记录。完整修改清单、命令、证据与限制见 docs/RELEASE_P1_REPAIR_PLAN_2026-09-13.md。

F1：EngineController 在追帧读取下一帧前用 av_frame_clone 持有候选帧引用，EOF 不再使用被源清空的借用 AVFrame；复用原缓存，不增加像素拷贝。F2：EnhanceGraph 在访问像素/上传资源前校验尺寸及像素格式，MediaFileSource 对中途变尺寸明确报错并锁存失败；普通文件不会自动重建图。F3：解码器区分 NeedInput/Frame/EndOfStream/Error，源拒绝损坏包/帧及硬解码错误，导出传播视频/音频读取错误，结束时全片解码校验输出帧数、尺寸、CFR PTS 与真实 EOF，成功后才提升 partial；验证可取消。ExportJobManager 保留子进程最终失败原因，避免早期失败被误写成已保留 partial。未修改音频调度或两个 P2。

scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/release-p1-20260913/build -RemotePlay（依赖完整参数见修复文档）初次 446 步、最终增量 31 步成功；日志 logs/release-p1-20260913/build.log、rebuild.log、final-build.log。新增 tests/integration/FileSafetyTests.cpp 与 scripts/gates/release-p1.py；python scripts/gates/release-p1.py --root . --build-directory out/release-p1-20260913/build --output-directory logs/release-p1-20260913/verified 最终 29 次进程调用及 12 项额外文件/音频/计数断言全部通过，总耗时 25.032 秒。覆盖正常/强制欠速 EOF、暂停 seek/resume、大小/零尺寸/非法格式、坏帧失败锁存及 seek/reopen、早晚损坏输入、变尺寸输入、输出损坏、验证取消、边界中止、实际导出 worker 失败消息、4K B 帧线程解码逐像素与 PTS 一致性。早期损坏返回 1 且无输出，晚期损坏/变尺寸返回 1 且只保留 partial；篡坏刚编码的尾包得到 decoded=59 expected=60 eof=false passed=false；健康输出 60/240 帧及音频时长正确。

首轮 regression/result.json 为 false：测试脚本将失败退出码误写为取消码 3，实际产品正确返回 1，文件状态断言正确；修正期望后 regression-final 全通过。自查去除重复状态初始化、增加提升输出前取消检查后重新构建，最终 verified 全通过；原失败证据保留。没有独立 Reviewer。

python out/release-p1-20260913/cross_checks.py 的 13 项调用全部退出 0，结果 logs/release-p1-20260913/cross/result.json；包含硬解导入、HDR Main10 软件/硬件路径、实时回放、NR 先行、欠速音频连续性、FG 恢复、图片尺寸、源保真、NTSC 导出及 NR/FG 实际欠速尾帧。RTX5070/616.56 实际 Feature18/DLSSG Create result=0x1、seh=0；欠速尾帧 nrEvaluated=16、nvofExecuted=2、generated=1、failed=false；Main10 硬解 confirmed=1，NR/SR 各 12、FG 11。NTSC 30000/1001 输出验证 90/90 帧，视频 3.003 秒、音频 3.000 秒。强制欠速测试不代表性能改善。

cross runner 调用 scripts/gates/delivery.ps1 -Root . -BuildDirectory out/release-p1-20260913/build 一次，23/23 通过，45.3460832 秒；证据 logs/delivery/7490c0d852a04aadbe96eced2106524f/result.json，status=software_short_gate_passed。包含实际 native4K NR/NVOF、D3D12 NVENC H.264/HEVC、FG、输出解码/音频、暂停 seek、图片及取消。

新程序 out/release-p1-20260913/build/veyra.exe，SHA256=B116AB6855D69CDEB29927AB3AD80422D54051339FCE47D7EE5939CAA987519D。patched avcodec-63.dll SHA256=0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F，与开工一致，未退回未打补丁的 FFmpeg；原版/社区 NR 身份符合已批准记录。未修改运行时、SDK、模型、凭据、个人配置或既有发布资产；未推送、打包上传或发布。

限制：变分辨率文件明确停止；全片 CPU 解码验证会增加导出收尾时间，但可取消，不是增强路径 GPU→CPU 回读。实机 PS5/PSN、HDR 显示器、长时稳定性、多 GPU、4K60 采集卡端到端验收未执行，本轮回放不代表实卡通过。下一项唯一任务：用户试用本轮新构建；正在运行的旧程序不会自动更新。两个 P2 按用户范围保留。


## 2026-09-13 GPU DIS 光流可切换实验接入

用户确认 PS5 清晰度基本与 chiaki-ng 相当，授权接入此前讨论的 GPU DIS 作为可选实验后端。开工存档 3ae4d5c 保留另一轮已完成的三个 P1 修复；tag checkpoint/gpu-dis-preintegration-2026-09-13，施工分支 codex/gpu-dis-integration。完整实施/测试/修改文件与边界见 docs/GPU_DIS_INTEGRATION_PLAN_2026-09-13.md。

专业光流选择增加 GPU DIS FAST，NVOF 默认及 FidelityFX 保留，预设枚举追加兼容；公开上游 cb7523b5104fc914dc501767c3139b43c2067af7 的 DIS source/shader 子集放 third_party/gpu-dis，保留 Apache/BSD 许可、来源及实际修改记录。只使用公共 D3D12 provider；没带工具箱 worker、SDK 或模型。图复用现有 A/B、parity fence、FlowAdapt、GPU 计时、reset 和 NR/SR/FG 消费者；增加 GPU 亮度适配、双向一致性与亮度置信度，未改变源显示颜色、音频、PS5、播放队列。不引入逐 pass CPU 等待或正常路径 GPU 回读。上游 shader 可见 SRV 写法按现有驱动 workaround 改为 staging；算法未改。包脚本递归带 DIS DXIL 子目录和开源 notices，未执行新发布。

vcvars64 下 cmake --build out/remoteplay/product-repair --parallel 6，完整增量 115 步通过；定向目标编译也通过，日志 out/gpu-dis-build.log、out/gpu-dis-rebuild.log、out/gpu-dis-final-build.log、out/gpu-dis-build-all.log。真实 RTX5070/616.56：veyra_experimental_backend_tests dis/dis1080/dis-xess/nvof1080；veyra_repair_fg_tests dis/dis-sr；veyra_repair_preset_tests logs/gpu-dis-20260913/preset-fixed.v1。每次进程 240 秒上限，实际各约 0.1–5.6 秒。640/1080 双向 DIS 46 次、方向/重置/resize 数值通过、D3D12 debug error=0；DIS+XeSS generated=43；DIS+NR+DLSSG 12 源/11 生成全部 contentValid，进一步 RTX Video SR 到4K同样11/11。NR CreateFeature18 result=0x1/seh=0，FG warm-up result=0x1，VSR op=0/2 result=0x1。不是屏幕扫描/真实 PS5 画质验收。

原预设测试新增后初次失败：测试没有删除刚加的 DIS 条目，导致旧条目数量断言失败。修复测试清理后，DIS 保存/重载相等与原有42项迁移测试均通过。保留原 results.json preset=1，最终 additional-results.json preset-fixed=0。详见 logs/gpu-dis-20260913/。

同一1080合成平移数据，各45个已完成GPU计时，DIS 光流中位17.2824ms、均值17.2826ms；NVOF中位1.15165ms、均值1.21748ms。此公开实现本机明显更慢，因此保留实验选项，不能宣传性能提升或默认替换。双向计算与其变分迭代走通用计算单元，其他硬件/素材未推断。用户肉眼比较游戏画质仍待执行。

powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair，23/23 PASS，47.156秒，logs/delivery/ca204954e1e64ee4aeddf7a41cf5f1e6/result.json。git diff --check及package-portable.ps1语法检查通过。最终veyra.exe SHA256=114E00EDD266182BD2556FD1150C487FE3C4B6348112A63AA7706BFCE554BCFA；patched avcodec SHA256=0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F，保持硬解slice修复。桌面PS5测试版快捷方式已核对指向out/remoteplay/product-repair/veyra.exe。

未测试真实PS5/采集卡新后端、多GPU、HDR屏幕、长时稳定性、Windows UI实际鼠标切换和新便携包部署；代码走既有后端切换重建/回滚，不能将静态接法当这些场景实测。仅本地构建与存档，没有push/Release/运行时修改，没有关机。下一项：用户重启桌面测试版，在专业模式的光流·运动估算选择GPU DIS，固定NR/SR/FG参数比较；速度/效果不满意即可切回NVOF。


## 2026-09-13 正式应用 1.0.0 整合、透明图标与截图

用户授权当前版本整合各修复分支、换透明 Logo、发布 GitHub 1.0.0，随后追加专业顶部截图。开工2c45419，存档checkpoint/pre-release-1.0.0-2026-09-13，施工codex/release-1.0.0。release-p1-20260913对应代码已在3ae4d5c祖先中，PS5相关修复分支均已包含；合并nrvideo/main的两次README更新并保留用户图片，不重复合入旧归档代码。细节见docs/RELEASE_1.0.0_EXECUTION.md。

替换透明PNG及9尺寸ICO，实际EXE资源验证alpha=0..255；版本资源1.0.0。顶部截图复用最终处理真实帧保存，唯一PNG到Pictures/Veyra Screenshots，无UI/窗口缩放/对比层；不声称精确截正在扫描的插值帧，原生HDR明确暂不支持。仅用户触发单次GPU回读；普通播放链路不增加回读。新增smoke-screenshot通过实际按钮处理器，1080输入NR+VSR+FG保存3840×2160，退出0、nrEvaluated261/generated221/failedfalse。测试代码仍只驱动产品模块。双语README、BUILD、Release Notes、组件/串流构建说明和source脚本更新。

cmd /c out/gpu-dis-build-all.cmd成功，最终EXE SHA256=95CE6F6236DC3A9CF90E68330A1FC999580BC3F66D4658EA6A96D6066EFF9159。delivery 23/23、44.737秒：logs/delivery/92e09ba3556148a9b9275d2ba1ef1bfe/result.json。单次测试低于300秒。完整便携包final目录生成、逐文件hash及禁止路径扫描通过；解压后的portable-smoke 5/5（PATH隔离、manifest禁用、运行时模块路径核对），日志logs/release-1.0.0/。包303913391字节，SHA256=EAAE5B13252EDE1F59DC41E3773C6DAFC960918EFDA914F160D1F3B03A34DBC5。

沿用七文件运行组件及社区NR HashMismatch记录，签名/哈希/许可证由publisher脚本核对。FFmpeg真实patched tree生成对应源码并校验header、5DLL、slice补丁哈希；RemotePlay对应源码固定Chiaki和静态依赖，带全部补丁及构建材料。无NVIDIA SDK/运行时/模型进入源码Git，未打包凭据/用户配置/测试媒体。新源代码资产仅开源依赖；完整包仅允许清单内runtime。未新增或修改运行时身份。新版本实际PS5长时、HDR屏幕、蓝牙/多GPU仍未验收；两个P2导出边界明确保留，没把应用正式版说成全部功能官方认证。发布结果在后续记录。

## 2026-09-13 1.0.0 公开发布完成

源码整合提交 2939cd4046e24f2b2fc987322f0196e7cc5acd2a 已 fast-forward 至 main，git push nrvideo main v1.0.0 成功。v1.0.0 标注标签固定该提交；本段是发布后的文档记录，不移动标签或替换已验证二进制。

GitHub Release https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.0 于 2026-09-13 06:38:08 UTC 公开。gh release edit v1.0.0 --repo Likely7/Veyra-NRVideo --draft=false --prerelease=false --latest 成功；随后读取 releases/latest，确认 tag=v1.0.0、draft=false、prerelease=false。六个资产均 uploaded，逐项服务端 digest、size 与本地 SHA256、长度一致，包括三个 ZIP 和三个校验文件。核对记录 logs/release-1.0.0/github-assets-verified.json（仅本地）。源码 v0.0.5..v1.0.0 新增/修改文件扫描未发现 DLL/LIB/EXE/ZIP/模型；工作区发布前干净。当前发布与对应源码包均已完成，真实 PS5 长时及其他未测硬件边界仍按上文，不由发布状态推定通过。

## 2026-09-13 README 项目来源与致谢

按用户要求，在中英文 README 最底部补齐实际集成/复用、架构与实现参考、已搁置 AMD NR 调研来源及各项目链接。依据 THIRD_PARTY_NOTICES、GPU DIS provenance、竞品/性能/采集审计与质量/AMD NR 方案核对，区分依赖和研究，不把尚未接入的算法宣传为现有功能。保留完整许可证及对应源码入口。仅文档改动，执行 Markdown 本地链接与双语项目 URL 一致性检查、git diff --check；无需重建或改动 1.0.0 包与标签。

## 2026-09-13 本地缓存与重复产物清理

用户授权检查并清理项目缓存。清理前扫描24.758GiB，执行out/cleanup-20260913.ps1预览和-Apply后移除1833个目标、12.656GiB，失败0；复扫12.103GiB。路径均解析为项目内out/logs，预检Git无跟踪文件、无重解析点、无正在运行的目标EXE。安全预览脚本首次祖先遍历未终止，已终止预览进程并修正为显式GetDirectoryName/Get-Item逐级验证；此前未发生删除。最终预览约3秒、清理约9秒。

删除：out/build旧构建；旧Release展开目录、0.0.2候选、1.0.0首轮候选ZIP（保留该目录内正式FFmpeg源码ZIP）；1.0.0最终包重复展开/验证目录；release-audit/package与旧audit/P1 build；RemotePlay关闭功能的验证构建；logs/delivery、optimization-goal-20260908、phase5中的合成PNG/JPG/MP4/partial/bin。对应.log/.json、测试源码保留；delivery的92e09ba3556148a9b9275d2ba1ef1bfe及ca204954e1e64ee4aeddf7a41cf5f1e6整轮产物保留。历史文档中指向已清理旧二进制/图片的路径不再存在，不代表重新测试或修改旧结论。

保留：当前out/remoteplay/product-repair完整构建与桌面快捷方式目标、chiaki-msvc-stage、SDK/模型/runtime_local、Git历史、源代码、真实captures、loop/local回归输入、正式1.0.0三个ZIP及旧最终版本ZIP备份。清理没有访问用户数据目录中的PS5配对/PSN凭据。当前构建仍使用外部C:/veyra-deps依赖，不退回stock FFmpeg。

验证：当前EXE、patched avcodec-63.dll和1.0.0 portable/RemotePlay-source/FFmpeg-source ZIP逐项SHA256与清理前相同；current CMakeCache、chiaki stage、最终delivery result存在；清理后git status无变更（本记录落盘前）、git diff --check通过。未运行产品或重新构建，因为没有改动产品文件。详细清单及哈希位于logs/cleanup-20260913/{plan.json,result.json,protected-before.json,after-size.json}，仅本地保存。旧out/build路径需重新配置/构建后才能使用，后续开发优先使用保留的out/remoteplay/product-repair。未修改GitHub Release资产。

## 2026-09-13 采集卡音频高延迟排查

用户反馈OBS音频正常、Veyra延迟高。确认产品音频ConnectDirect前缺少IAMBufferNegotiation，音频sink仅请求1个sample最小字节。参考OBS libdshowcapture固定c13d4b7及Microsoft API，独立增加连接前10ms帧对齐请求、失败兼容继续、实际allocator容量日志；新增实际inputBlockMs/inputIntervalMs/inputBlocks诊断，不改自动补偿和音频重采样。不能推定反馈者驱动确实500ms，未取得其日志/实卡听测。

开工tag checkpoint/capture-audio-latency-20260913。旧产品合同32 PASS/1 FAIL验证缺口；修后38 PASS，完整构建100步成功；合成PCM+真实WASAPI同步16 PASS、抖动5秒additionalReset/underrun/missing全0、设备恢复PASS。delivery 23/23、53.123279秒，logs/delivery/c7ecf48ee7ae44a99cc3eb6f971d61fc/result.json，NR实际Create/Evaluate成功、SEH0。当前EXE B847BFAA3440548C8494DB5DE90280BAB76609DB5B5DECCFF3ABB0870C6DB89A，FFmpeg slice补丁DLL哈希不变。详细命令、失败、修改文件与验证边界见docs/CAPTURE_AUDIO_LATENCY_2026-09-13.md和logs/capture-audio-latency-20260913/。未修改发布包、SDK或运行时，无push。下一步反馈者新版实卡测试与日志，10ms请求不是总延迟承诺。

## 2026-09-13 1.0.1 发布准备与验证

用户明确授权发布1.0.1。codex/release-1.0.1整合音频修复79734ff、用户日志分析eabf3c7和远端用户README编辑77c2c04，保留用户删除。更新版本资源、双语README、组件/构建说明、Release Notes及打包文档选择。当前用户打开的EXE阻止链接，未强关，改用相同构建对象与1.0.1资源的Ninja实际命令链接至隔离输出；EXE版本1.0.1，哈希073B72E2D6C44045036684B115CEA99F54FCD10F52D4BA4FA79C191C54DA94BE。

完整便携包与两份对应源码包制作成功，六个发布资产；七个已批准运行组件与PS5 patched FFmpeg不变，无SDK/运行时加入源码。便携ZIP逐文件清单校验及源码扫描通过，FFmpeg六个.mp4后缀文件确认是上游ASCII测试参考文本。新EXE解压后独立启动/实际增强5/5 PASS，最长7.92秒，社区/原版/Video SR组合均有NR执行和FG真实输出；此前产品代码delivery 23/23仍单独记录为版本资源更新前验证。具体命令、失败与恢复、资产哈希、运行码见docs/RELEASE_1.0.1_EXECUTION.md，日志logs/release-1.0.1。反馈者实卡音频改善仍待其新版测试，未声称10ms端到端。下一步推送源码及六资产并核验公开Release。

发布完成：main及v1.0.1（c0cba4b）已推送，六资产服务器大小/摘要与本地一致，2026-09-13T14:53:27Z正式公开为latest，非草稿/非预发布，Release ID 387931939。链接https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.1 。未修改1.0.0资产，未关闭用户正在运行的程序。发布后仅补记文档，不重打包、不移动tag；待反馈者下载新版进行实卡音频验收。

## 2026-09-13 PS5 断流后重连修复（本地）

用户反馈长时串流卡住、自动及手动连接均不能恢复，必须重启。存档checkpoint/ps5-reconnect-20260913，分支codex/ps5-reconnect-repair，基线e3d6c3c。旧现场日志被启动截断，自然断流根因不明；修改GUI仅初始化一次日志、追加保留跨重启记录并独立处理override/多进程。新会话首帧统一30秒，已有画面的断流仍6秒恢复；初次仅RP_IN_USE允许最多3次退避重试，拒绝认证不重试。失败保留具体原因/终止码，增加资源结束前后日志，不无依据提示重新配对。

用户明确停流授权实机：旧可控自动恢复PASS，未复现自然停帧；新版同一进程取消恢复后手动重连两轮PASS，真实捕获占用码4，第二轮3次占用后恢复，画面/FG/音频均推进且idle=1。新版可控自动恢复PASS（attempt2，463健康采样），真实NR/4K DLSSG Create=0x1、SEH=0。core75/75、contract107/0、boundary通过，完整99步构建通过；双GUI日志重启检查PASS。每次测试210秒以内上限。详细命令、修改清单、证据及限制见docs/PS5_RECONNECT_REPAIR_2026-09-13.md，logs/ps5-reconnect-20260913/。

已正常关闭用户空闲旧GUI，更新桌面快捷方式的同一EXE，SHA256 23791CE1A316A4CFDAA9D292502E4620CB9E74BAA07E5310F82D843A53FAE326；FFmpeg切片补丁不变。无SDK/二进制/配对进入Git，不改已发布1.0.1，不push。下一步用户长时游玩确认；最初自然停帧原因仍需新日志，不能宣称全部断流根除。

## 2026-09-13 1.0.2 连续故障重试额度修复与发布准备

新现场23:16:42完整输入停止、缺包/组帧/传输错误激增，用户确认PS5 Wi-Fi，事后ping正常不能排除瞬时故障；23:16:48因累计3次已耗尽而不再重连。用户授权修复并发布1.0.2，checkpoint/ps5-retry-budget-20260913，codex/release-1.0.2。改为连续解码30秒、无1秒断档后恢复本轮3次重试；累计连接编号不归零，sourceEpoch保持隔离，退避和UI改用本轮次数。包含此前aad8ce4的占用重试/首帧等待/日志保留。

完整构建21步通过；核心77/77；真实PS5两次可控断流115秒PASS，第二次在稳定30秒续期后注入，画面/NR/SR/DLSSG/音频恢复，2319健康采样，idle=1，NGX Create/Release=0x1、SEH0。便携初次7秒smoke在原版FG初始化/seek后仅预热即结束，不能计通过；添加可选测试时长参数，15秒观察5/5通过，保留真实生成及包内模块路径断言。EXE B693547F722C01E583F1B549507E355540618B628740147154A212F61D3BFFE2，桌面同路径已为1.0.2。

七运行文件和patched FFmpeg不变；便携/两对应源码ZIP及SHA256共六资产，逐文件和源码排除检查通过，未放入SDK/用户媒体/凭据。更新双语README及Release Notes、组件和构建说明。详见docs/RELEASE_1.0.2_EXECUTION.md，证据logs/release-1.0.2。软件恢复改善不等于无线故障根治。

发布完成：main与v1.0.2（09c054c）已推送至nrvideo。六资产全部uploaded，服务端字节数及SHA256逐项匹配；2026-09-13T15:32:29Z正式公开为latest，非草稿/非预发布，Release ID 387942841，API复核通过。地址https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.0.2 。发布后只追加本记录，不移动标签、不修改旧资产。桌面快捷方式所指EXE已为1.0.2；下一步用户长时游玩验收。

## 2026-09-14 采集格式名称与原生格式扩展（本地）

用户要求修复P010/RGB24显示GUID并补齐常见格式。基线236228c，分支codex/capture-formats-20260914，checkpoint/capture-formats-20260914。原生格式从YUY2/NV12/RGB32扩至15项，包含RGB24/ARGB32/RGB555/565、UYVY/YVYU、NV21/I420/IYUV/YV12、P010/P016；按内存布局重排并保留高位深，独立于HDR开关，不全局放开采集HDR。名称、布局、色彩不支持和系统转换路径明确区分。

构建曾因正在运行的EXE占用失败，用户关闭后62步及最终4步增量通过。127项布局/边界、30组GPU采集颜色/精度、8组既有HDR回归通过；GPU测试最初误选8位呈现资源，改取真实FP16 ingress后通过，失败日志保留。delivery 23/23、56.2209961秒，logs/delivery/544cd7222e404abcb7a760a63f1c546c/result.json；RTX5070原生4K NR Create=0x1/SEH0/Evaluate12。具体命令、文件与失败见docs/CAPTURE_FORMATS_PLAN_2026-09-14.md及logs/capture-formats-20260914。

本机USB3 Video枚举/SetFormat/ConnectDirect成功，Run返回0x800705AA资源不足，未算实卡通过；当时OBS运行，未证明占用原因。反馈者Live Gamer Ultra 2.1不在本机。桌面EXE已更新，资源版本仍1.0.2、SHA256 B62CA082176CE02D690D302236BEBC86ABD557CA18243CDD2000E21645A49BCC，patched FFmpeg不变。源码不含SDK/二进制/日志/凭据，未发布；下一步反馈者新版实卡验收。用户正在剪辑，后续Smooth Motion本轮只写方案，不再跑GPU测试。
## 2026-09-14 Smooth Motion执行方案（未施工）

用户正在剪辑，要求只写接入方案。新增docs/SMOOTH_MOTION_EXECUTION_PLAN_2026-09-14.md：推荐Veyra专属NVAPI DRS配置管理、默认关闭、与内部DLSS/XeSS互斥；先验证驱动实际接管再实现UI。定义可回滚事务、程序匹配/共享profile冲突、重启生效状态、统计不可测边界、音频/截图/导出/OBS分离及分阶段验收。核对NVIDIA官方说明和公开nvapi.h的DRS接口、Profile Inspector固定提交2f50c388b3a4d661cade66b32746bec096d1eee1的设置ID。未修改驱动、未执行Smooth Motion GPU测试、未发布。下一步用户空闲后做可回滚的Veyra程序级可行性测试。
## 2026-09-14 — 普通版 Smooth Motion 开启说明，不强制互斥

- 用户实测反馈 Smooth Motion 有效且稳定，并明确取消软件内管理/强制互斥方案：只用驱动补帧可在软件选择关闭补帧，允许与内部 DLSS/XeSS 同开。叠加效果未验证，不宣传更好。
- 从普通构建基线6d0ec99建立codex/smooth-motion-help；原受限实验分支codex/smooth-motion-experiment保留在27c17eb。普通版没有实验FG禁用逻辑，也没有新增驱动检测/配置写入。
- SettingsWindow 在倍率下方增加可展开的“Smooth Motion · 开启方法”，同主题展示，按宽度/DPI计算高度，下移后续参数，收起恢复。说明软件开关不控制驱动、叠加未测、指标不含驱动部分、截图/导出及音频/直播边界。更新双语README、AGENTS和原方案状态；具体记录docs/SMOOTH_MOTION_HELP_2026-09-14.md。
- 实际构建：VS x64 cmake --build out/remoteplay/product-repair --target veyra veyra_ui_contract_tests veyra_repair_preset_tests --parallel 4，通过。UI合同（含384组四种DPI布局）通过；预设（含42组后端迁移）通过。第一次预设测试遗漏文件参数退出2，补上out目录独立临时文件后通过，日志保留。
- 临时检查脚本out/smooth-motion-help-ui.ps1只操作自己启动的隐藏空载测试实例，确认说明默认收起、展开无重叠、收起恢复、内部FG控件启用、DLSS/XeSS倍率4/2项保留。空载GUI12秒，退出0。git diff --check通过。证据logs/smooth-motion-help/。
- 普通EXE已本地替换，资源版本仍1.0.2；SHA256 61C70478840A7C3961CA299CCEB5371609218F147D7E0046333209FEB3155963。patched avcodec保持0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。无SDK/DLL/模型/配置/日志入Git，未push或发布。
- 本轮未执行NR/FG Create/Evaluate、实卡/PS5串流、驱动补帧或叠加实测；只变更说明及其布局。下一步用户在NVIDIA App为普通版veyra.exe单独配置后自行对照效果，旧实验EXE的配置不自动搬迁。

## 2026-09-14 — 发布1.1.0准备与验证

用户授权发布并在完成后关机。codex/release-1.1.0，存档checkpoint/pre-release-1.1.0-20260914。包含普通版Smooth Motion教程与允许叠加策略、此前采集格式扩展；不合入强制互斥实验构建。更新README中英文功能表/教程、1.1.0版本及发布/组件/源码说明。
完整构建通过；delivery23/23，46.8241855秒（logs/delivery/f3bba8f704634831ab09ab62e7c233b7），真实NR Create0x1/SEH0、原生4K Evaluate12，播放和导出通过；采集布局127、GPU颜色/HDR38通过；独立解压便携5/5通过，真实内部FG生成664/669/669帧。细节、命令、未执行范围见docs/RELEASE_1.1.0_EXECUTION.md。
便携/RemotePlay源码/FFmpeg源码及各自SHA256共六资产生成，逐文件及排除扫描通过；七运行文件与patched FFmpeg沿用，社区NR继续HashMismatch，源码没有SDK/DLL/模型/凭据。桌面普通程序已为1.1.0，EXE SHA256 F4106617DD743E2913729D3BDF9DF8A8DE911E1E28EAC482F3204ECD4967AE39。下一步推送main/tag、上传草稿、核实服务端digest后公开latest，再保存记录关机。

1.1.0发布完成：main/v1.1.0（1523ddb）已推送；2026-09-13T18:50:38Z公开为latest，Release ID 387998437，非draft/prerelease。六资产均uploaded、服务端size及SHA256逐项匹配；远端README与标签一致。地址https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.1.0 。证据logs/release-1.1.0/published-release.json、remote-assets-verified.json。未移动tag或修改验证后的压缩包。本记录保存后按用户明确请求关机。

## 2026-09-14 — 精简双语 README 致谢

用户授权将确认的简短致谢更新到 GitHub。先 fetch 并 fast-forward 到用户远端修改 83de594，保留其 README 内容。README.md / README_EN.md 底部统一为 Magpie Experimental（研究启发）、chiaki-ng（串流基础）、XeSS-GPU-Motion（GPU DIS 实现）及第三方说明链接；原完整依赖、参考和已搁置 AMD 调研清单移入 THIRD_PARTY_NOTICES.md，原许可证与组件记录保留。git diff --check 通过，核对双语段落及迁移后的相对链接。仅文档改动，未构建、未执行 GPU Create/Evaluate、未修改运行组件或 Release 资产。随后提交并推送 main。

## 2026-09-14 — 用户提供 NeuralScreen 1.8.2 的 RTX30 调研

仅静态读取用户包、Get-FileHash/Authenticode/VersionInfo、dumpbin exports 和包内固定提交的 worker 源码。确认 DLL 为 DCC0DC24…/165840496 bytes/310.8.0.0/HashMismatch，与包清单一致但不同于已批准两版；五项 NGX 入口存在。架构查询进程 hook 是额外兼容条件，发现索引0/未知句柄回退、无恢复生命周期等不适合直接照搬的边界。详见 docs/RTX30_NEURALSCREEN_AUDIT_2026-09-14.md。未启动第三方程序、未加载/复制/修改 DLL、未执行 Create/Evaluate、未构建；没有 RTX30 实机验收。git diff --check 通过。当时未提交、未推送、未发布；后续开工前提交为11977ab。

## 2026-09-14 — RTX30 NR 实验选项与首次默认全关

- 用户授权后在 codex/rtx30-nr-safe-defaults 施工。NR选择器追加RTX30独立档，复用现有处理图和重建事务。独立编写仅NR模块作用域的架构查询适配，按D3D12 LUID匹配显卡；只兼容成功查询的选中Ampere，未知句柄不猜索引0；恢复IAT再卸载，不改系统驱动入口或磁盘DLL。
- 用户指定DCC0DC24…组件原样置于忽略目录 runtime_local/nvidia/nr-ampere/，哈希与原件一致、HashMismatch不伪装为原版。未扩大Release资产白名单，未把组件/SDK/配置加入Git。THIRD_PARTY_NOTICES追加一行NeuralScreen行为参考归因。
- EnhancementSettings/PlayerOptions/UiSessionState及启动UI统一首次NR/SR/FG全关；保留已有预设/上次确认设置。便携扫描禁止个人配置，统一delivery显式传--nr，避免默认改变后漏测NR。
- 实际执行out/release-1.1.0-build.cmd构建通过；预设回归（含42迁移及48架构策略组合）、UI合同（首次全关/已有参数恢复/布局）、真实NVAPI三次装卸通过。命令与日志见docs/RTX30_NR_AND_SAFE_DEFAULTS_2026-09-14.md。
- 本机RTX5070加载实验组件：Feature18 Create=0x1、SEH=0；12秒软件smoke输出162帧且NR Evaluate162次、NVOF161次、failed=false。新增共享Engine同进程切换测试16项通过：全关→Ampere→Original→Community→Ampere→全关，四张实际3840×2160 PNG有效；两次兼容卸载restore=true。未执行RTX30真机，5070查询无需架构改写，不能据此声称30系成功。
- delivery23/23，47.26秒，logs/delivery/e4d638da6ada4df58e208c1545a35171/result.json。播放、暂停seek、截图、原生4K、带音轨NVENC H264/HEVC及取消通过。最初单独--smoke-save未生成截图，之后同进程测试保存实际PNG补齐输出验证；历史追加日志已按本次时间划界。
- 本地out/remoteplay/product-repair/veyra.exe（现有桌面PS5测试快捷方式目标）SHA256 7E3A86371544D27173195E9D6E9071B467CB6261F2E1370D2FF3AC1E69F785B5。未改GitHub下载包、未push/发布；下一步RTX30持卡用户验收NR单项、耗时、显存与切换。没有PS5/采集实卡、长时或Smooth Motion叠加的新验收。

## 2026-09-14 — 1.1.1 发布准备与验收

用户明确授权发布新版本。分支codex/release-1.1.1，存档checkpoint/pre-release-1.1.1-20260914，包含831432f的RTX30实验选择器和初始全关；八增强运行文件按manifest检查，新增DCC0DC24…组件保留HashMismatch，与已批准40社区版分别独立；不修改磁盘DLL，不放入Git。

版本资源1.1.1构建通过，EXE SHA256 4C7144A6F3160AF9A0B6446F40A7CC7A7E36AB65B76F9E9ADA9613FEBBF7FF99。同轮产品基线已有delivery23/23，此次版本/打包增量做独立解压便携7/7：首次默认全关824帧、Ampere NR725帧、原版与社区/VideoSR内补帧均实际执行；包内路径/去manifest/隔离PATH通过。三ZIP逐文件审计通过，无SDK/凭据/个人配置混入；patched avcodec仍0710F0D8…，对应源码和许可证保留。

更新双语README、Release Notes、组件/源码说明；命令、资产大小/哈希、测试证据见docs/RELEASE_1.1.1_EXECUTION.md和logs/release-1.1.1/。RTX30实卡尚未验证，未声明全型号成功。下一步快进main、推送tag、草稿上传后核对服务端六资产digest再公开latest；本次无关机请求。

1.1.1已发布：146f035已合入并推送main，v1.1.1保持此发布提交。2026-09-14T07:10:02Z公开latest，Release ID388201201，非草稿/预发布。六资产服务端size/digest均匹配本地审计；完整便携ZIP421715775字节，SHA256 17D1F9C6A56043014E62F598AB1C6DA492DF5BC040168B42AA9D836594951D6A。公开地址https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.1.1 ，证据logs/release-1.1.1/published-release.json。未修改已验证ZIP或移动tag；后续仍等待30系持卡验收。

## 2026-09-14 — HDR全增强与5.1可行性研究

用户要求研究完整HDR输入/增强/输出和5.1能否全部实现。本轮读取当前源代码、固定SDK本地文档/样例、NVIDIA/Intel/Microsoft/Opus官方文档及Chiaki上游源码；无产品代码修改、无构建、无新Create/Evaluate/主机连接/声卡测试、无组件变更或发布。

结论与分阶段计划落盘docs/HDR_ALL_EFFECTS_MULTICHANNEL_RESEARCH_PLAN_2026-09-14.md。发现HDR限制同时在Engine和图入口，增强前先SDR映射；NR已有浮点原底/代理/残差结构，可研究保留HDR原底的变化合成，不必把NR模型原生HDR当唯一出路。DLSS SR有HDR接口；DLSS FG/XeSS公开合同要求HDR10/RGB10，不能直接用当前scRGB。RTX Video的10-bit样例仍要求SDR，不把10-bit或TrueHDR转换当原生HDR保留证据。

音频需端到端声道布局与统一音频帧计数：文件当前降混，采集入口当前直接拒绝>2声道，纠正“所有输入都混成立体声”的笼统说法。当前Chiaki单流Opus链仅1/2声道，PS5真实5.1需另找到上游协商/传输证据；无法从2.0恢复六个独立声道。HDR合成路线尚属待测设计，不能以文档当实现完成。git diff --check通过，未推送研究文档；下一步先验证NR保留HDR与RGB10补帧组合的隔离原型，再贯通全部入口/5.1/导出及UI。

## 2026-09-14 — HDR/多声道实施节点 A
用户授权施工、PS5真实多声道排除。本地分支codex/hdr-multichannel，checkpoint/pre-hdr-multichannel-20260914。HDR基底保留、NR/VideoSR代理合成、RGB10 DLSS/XeSS补帧首轮GPU通过；20帧真实NR/SR及18/16生成，1000nit输出998.932nit，零残差广色域/高光身份通过。完整构建成功，具体命令/失败/证据和未测边界见docs/HDR_MULTICHANNEL_EXECUTION_2026-09-14.md。其他入口、音频及产品收尾继续施工，未发布。


## 2026-09-14 — HDR/多声道实施节点 B：软件交付、等待实机验收

完成文件/P010-P016采集PQ与HLG输入、手动颜色覆盖、HDR保留NR/两种SR/DLSS-XeSS补帧、HEVC Main10 PQ导出、浮点JXR截图；文件/采集PCM保持声道掩码，共用音频时钟/增益/补偿，输出端明确降混，采集上游6ch同样协商10ms。PS5双声道保持，不伪造5.1。双语README标明本地开发、未发布，致谢仍在底部。

构建命令cmd /c out\release-1.1.0-build.cmd通过。最终delivery23/23、44.67秒，logs/delivery/e344611bab454e2d9a23c510aac08215/result.json；EXE 9F28BE16AC94AA33F013C7A29E32E8CE49753C445BE10AA22E572C948EEFD045。pipeline52/52、capture contract零失败、30采集GPU+16HDR颜色用例通过；三NR运行库在5070组合20次Evaluate、DLSS18/XeSS16生成；六声道28检查通过，快/慢时钟各120秒通过；文件音频时间线、欠速和抖动最终exit0。HDR4K NR+VideoSR+FG Main10导出24帧完整解码，六声道音轨保持；JXR最终逐像素比较通过。patched FFmpeg哈希仍0710F0D8…284F。

本轮发现并纠正FP16截图只拷半行、六声道上游缓冲仍限定8字节、旧测试/播放器probe将单声道送进立体声renderer；首轮JXR自回读不能证明原图完整、首轮audio-timeline包含4失败，均保留失败日志并由独立检查/重跑覆盖。其他编译/夹具失败和真实Create/Evaluate结果详见docs/HDR_MULTICHANNEL_EXECUTION_2026-09-14.md及logs/hdr-multichannel。没有把失败删掉或把尾部PASS当整套通过。

桌面Veyra PS5测试版快捷方式指向当前构建。当前桌面HDR未启用、音频输出2ch，真实UI测试明确走HDR转SDR和6→2降混；HDR输出数值/接口验证与HDR实屏是分开的证据。未测真实5.1扬声器、HDR实卡、30/40系列、PS5新会话、OBS HDR及跨显示器切换。只完成软件实施，不宣称所有设备和画质验收。下一步用户在HDR屏与真5.1设备上验收；没有新push、Release、运行库替换或关机。


## 2026-09-14 — 手动转为 SDR 显示

用户授权增加 SDR 输出开关。采集面板默认关闭的“转为 SDR 显示”立即作用于所有预览，源 HDR 元数据保留、总增强关闭时也生效；启动/显示器轮询/尺寸变更/实时事务统一输出策略，避免轮询恢复 HDR。v12 预设保存，旧设置默认关闭；导出 HDR 合同不变，截图跟随预览。双语 README 更新，致谢仍在底部。

最终构建 cmd /c out\release-1.1.0-build.cmd 成功；首次旧进程占用造成 LNK1104，正常关闭后重建成功，失败日志保留。48 组预设迁移与 roundtrip、UI/输出策略合同、30 采集 GPU 颜色 +16 HDR 组合通过；真实 UI 加载 PQ 文件，在总增强关闭下连续切换/重开面板通过，源保持开启，日志记录实际 HDR 输入/SDR 输出。delivery23/23、45.20秒，logs/delivery/6a7c3d4a27e140ec9145e8e016bc7453/result.json；NR Create0x1 SEH0、60次Evaluate。EXE SHA256 9D3DFFA2D397F00F512B962A349453430CD3F0039ECF55FAB9961279291B2C76，patched FFmpeg未变。

完整命令/文件/证据与失败见 docs/HDR_MULTICHANNEL_EXECUTION_2026-09-14.md C节。当前Windows HDR关闭，物理HDR↔SDR交换链与实卡视觉切换未测；下一步用户用桌面测试版在HDR设备上验收。只更新本地开发版，没有push/Release。

## 2026-09-14 — 1.2.0 发布准备与验收

用户明确授权发布 1.2.0 到 Likely7/Veyra-NRVideo。整合 HDR 全增强/导出/截图、文件与采集 5.1 PCM、手动 SDR 预览开关和既有 PS5 路径；运行时保持既有八文件，FFmpeg PS5 slice patch 保持。更新双语 README、Release Notes、组件清单、构建与对应源码说明；新装默认仍关闭增强。

1.2.0 EXE SHA256 E49D90E1217E0DE1B59C9C889DE318DC46337DF51FCF838038D7F5AF896EE457。delivery23/23、48.85秒；HDR 五种真实 GPU 组合、30采集颜色与16 HDR颜色组合、预设/SDR开关、六声道与音频抖动均通过。最终完整便携包在独立解压、隔离PATH、临时移走manifest后7/7通过；首次默认无增强。三ZIP逐文件审计通过，无SDK、凭据、个人配置、日志或测试媒体混入。所有资产、命令、哈希和未执行实机边界记录于 docs/RELEASE_1.2.0_EXECUTION.md。

待执行：提交、推送 main 与 v1.2.0、上传草稿、核验 GitHub 服务端六资产尺寸/digest 后公开 latest。真实 HDR 屏/采集卡、5.1 扬声器、PS5新会话、RTX30/40 实卡仍不因本次发布被伪装为已验收。

1.2.0 发布完成：main 的发布提交 62be2ef187352bfefe8c264cdef24d3a89c1beff 与带注释标签 v1.2.0（ab1bf0ce1644cfe895ba8fd32fc7de7f1d520834）已推送。Release ID 388272314 于 2026-09-14T09:22:30Z 公开为 Latest，非草稿、非预发布：<https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.2.0>。六个资产服务端大小和 SHA-256 逐项匹配最终本地包：便携包 421736651 字节，SHA256 0A0D65DA75BE45F79587C1CBABE33062AFB32D2007F59E9270DFDDDA0973687E；对应 RemotePlay 与 patched FFmpeg 源码包及三份 .sha256 同步公开。证据为 logs/release-1.2.0-upload/remote-assets-verified.json；未更改已核验的 ZIP 或移动发布标签。物理硬件验收边界保持上述记录。

## 2026-09-14 — 媒体文件兼容性研究与计划

用户反馈 AV1、MOV 等文件“不能支持”，授权先研究并给出方案。本轮只读取源代码、1.2.0 随包 patched FFmpeg 构建记录和本机 DLL ABI，不修改产品功能、不构建、不更换 FFmpeg、不发布。实际 `avcodec-63.dll` 查询确认 H.264/HEVC/AV1/VP9/ProRes/DNxHD/MPEG-2/MPEG-4/VC-1 均有软件解码器；FFmpeg 配置与源码确认 MOV/MP4、Matroska/WebM、AVI、MPEG-TS 解封装器存在。当前问题不是一个 AV1 allowlist：文件对话框已有 MOV/AVI/TS 与所有文件，而普通文件又硬编码关闭硬解、图创建前不能可靠得到首帧的位深/HDR信息、失败提示过于笼统。FFmpeg 配置禁用了 libdav1d，保留原生 AV1，不能凭“能解码”承诺性能。

完整的可回退分阶段实施/验收计划见 docs/MEDIA_FILE_COMPATIBILITY_PLAN_2026-09-14.md。重点先做预检诊断和安全的 Auto 硬解→软件回退，再贯通首帧色彩契约与样本矩阵；dav1d/重建 patched FFmpeg 仅在真实性能基准证明必要后独立审计。未拿到用户问题文件或其日志，不能断言当前失败的具体 codec/profile/metadata 原因。

## 2026-09-14 — 媒体文件兼容性施工：AV1/MOV 与首帧/硬解回退

在 `codex/media-codec-compatibility` 隔离分支施工。`SourceInfo` 现在记录容器、视频 codec 和首帧像素格式；普通文件先探测首个有效视频帧再创建 EnhanceGraph，图描述会收到文件位深；文件路径默认尝试 D3D12VA，D3D12 纹理不是单层 NV12/P010、硬解报错或首帧导入契约不满足时，在首帧前原子重开软件解码并记录原因。日志补充首帧实际 format、HDR/matrix/transfer/range/chromaLocation；FFmpeg D3D12VA 导入增加纹理维度、mip、sample、尺寸与 DXGI 格式校验。没有改 PS5/采集卡入口的协议。

真实证据：旧 1.2.0 patched FFmpeg 的 AV1 MP4 软件探针虽找到 `av1`，首帧失败 `code=-40 Function not implemented`；因此“枚举到解码器”确实不是“可以播放”。使用项目外 vcpkg `dav1d 1.5.4`（Apache-2.0/BSD-2-Clause/ISC/MIT）并保留 PS5 H.264 32→256 slice patch 重建独立 FFmpeg prefix；配置含 `--enable-libdav1d`、许可证仍为 LGPL 2.1+，`avcodec-63.dll` 对 `dav1d.dll` 的动态依赖已由 dumpbin 核实。新运行目录中：AV1 MP4 软件解码30帧 PASS（实际 codec=libdav1d）、ProRes MOV软件解码30帧 PASS；新五个FFmpeg DLL + dav1d 下 H.264 D3D12VA/共享设备/NR 12帧 PASS；旧 H.264 软件源测试23/23 PASS。新 prefix 的五个FFmpeg DLL和dav1d哈希、外部依赖许可记录在本机 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed/share/ffmpeg/veyra-local-build.json`，未进入Git。

构建命令 `cmd /c out/release-1.1.0-build.cmd` 最终成功；中间一次 `veyra.exe` 链接遇到旧进程/临时锁，重跑后通过。`git diff --check` 待本轮收口时执行。当前新FFmpeg尚未替换1.2.0 GitHub Release资产，也未制作/上传包含dav1d源码、port、版权和SPDX的对应源码包；不能把1.2.0写成已支持AV1。未执行用户原始AV1/MOV文件、长时播放、所有10/12-bit/4:2:2/4:4:4组合及实机画质验收；下一步补运行脚本/便携审计与真实样本矩阵，再决定是否制作新版本。

## 2026-09-14 — 媒体兼容性施工收口（本地分支，未发布）

在 `codex/media-codec-compatibility` 完成首个可运行闭环：`FFmpegVideoDecoder` 对 AV1 优先选择可选的 `libdav1d`，无该后端时保留 FFmpeg 原生回退；文件源默认尝试共享 D3D12VA，首帧前发现解码错误或 D3D12 纹理不满足 2D/NV12/P010/尺寸契约时自动重开软件解码；图创建前消费首帧缓存，避免 AV1/MOV 的真实位深和 HDR 信令被错误地按 SDR 建图。构建脚本支持 `-FfmpegRoot`，并只从所选前缀把 `dav1d.dll` 放到应用目录；便携脚本同步复制 DAV1D 版权/SPDX，未改变源码仓库的二进制隔离规则。文件源头文件注释已更新为按 FFmpeg 实际容器/codec 能力描述。

本机外部依赖 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed` 已补齐 `dav1d.dll`（SHA256 `38E09F960822A081FC46FC296FB3EF5F841D1A6C15A39F20684C2F9D88A9FC52`）及对应许可证；FFmpeg 配置实际含 `--enable-libdav1d`，保留 PS5 H.264 32→256 slice patch。构建命令为：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 ... -BuildDirectory out/media-codec-dav1d -FfmpegRoot C:/veyra-deps/ffmpeg-ps5-dav1d-installed`，478/478 编译链接通过，输出包含五个 FFmpeg DLL 和 `dav1d.dll`。PowerShell 三个脚本语法解析均为0错误，`git diff --check`收口通过。

验证结果：新 `veyra_media_probe.exe` 的 AV1 MP4 软件解码30帧 PASS（日志明确 `codec=libdav1d`）；ProRes MOV 软件解码30帧 PASS；同一新运行目录的 H.264 D3D12VA/共享设备/NR 12帧 PASS（NGX Create/Evaluate/Release 为 `0x1`、SEH0）；H.264 `veyra_source_tests` 23/23，软件媒体探针30帧 PASS；`veyra_quality_probe` 对 AV1 30帧、无增强图完整处理，`failures=0`；独立便携包只关闭增强的完整播放器 smoke 运行3秒，`smoke frames=60 generated=0 failed=false`，自动从 AV1 D3D12VA 回退到 `libdav1d` 并保存/显示正常。实际合法的 AV1-MOV 样本无法由当前 FFmpeg MOV muxer生成（其明确拒绝 AV1 写入 MOV），因此没有把“AV1 MOV”写成已验证格式；MOV 按容器内实际 codec 分开判断。

本地 `package-portable.ps1` 已成功生成带 `dav1d.dll`、DAV1D 版权/SPDX 和 FFMPEG provenance 的1.2.0测试包。`portable-smoke.ps1` 的基础、社区NR、DLSS NR/FG场景产生了有效输出；最后的既有 VideoSR 断言因当前驱动未加载脚本要求的 `_nvngx.dll` 失败，日志保留在 `out/media-codec-dav1d-portable-smoke/video-sr-nr-fg.stdout.log`，不归因于AV1/MOV。新 FFmpeg 仍未替换1.2.0 GitHub Release，未制作/上传包含 dav1d 对应源码/port 的正式对应源码包；用户原始文件、长时播放、10/12-bit、4:2:2/4:4:4 和其他显卡尚未验收，不能宣称所有格式和设备均已支持。

## 2026-09-14 采集音频爆音与沙沙声修复（本地）

用户反馈采集卡音频偶发爆音、沙沙声，且已有多名用户反馈。排查确认采集音频格式可能按 DirectShow 枚举顺序先选 44.1 kHz；本机 USB3 卡实际 48 kHz 在后面。旧实时 renderer 在欠载时会直接停止/重置，WASAPI 实际共享缓冲约 22ms 而采集回调按10ms协商、启动只预填约10ms；另外 `validBits` 未贯穿，24-in-32/packed 24-bit 存在解释风险。

本地施工增加48kHz优先的媒体类型选择、格式/validBits诊断及PCM归一化（16/32/packed24）、非有限值/削波/填充检查；采集端点请求20ms并预填20ms。短暂实时欠载改为不写伪造媒体静音帧、不推进媒体时间线，持续约30ms且输入同时缺失时先淡出，再重置PCM/重采样/漂移校正并重锚，避免硬停止造成 click。UI 分开展示欠载次数、缺口和真正插入的静音；欠载日志限频。物理回归测试增益固定0，避免听感干扰。

实际构建：`scripts/build.ps1 -Preset x64-release -BuildDirectory out/build/audio-artifact-repair-20260914 -FfmpegRoot C:\veyra-deps\ffmpeg-ps5-dav1d-installed`，完整增量29/29、物理测试变更增量2/2，均 exit0。初次构建曾因 `CaptureAudioSession.h` 直接引入 `ks.h` 与工程 `GUID_NULL` 宏发生 include 顺序冲突，改为前置声明后恢复。`veyra_capture_audio_tests.exe` exit0；`--jitter` 与 `--jitter --5.1` 均 exit0（500×10ms，追加reset/underrun=0）；`veyra_multichannel_tests.exe` `checks=31 failures=0`；`veyra_audio_timeline_tests.exe --jitter` 1x/2x/4x均0 underrun。真实本机 `veyra_capture_tests.exe capture:0:0:0:0` exit0：选择48k/16-bit/16 valid bits，收到31个音频块，peak0.01043，欠载1次/576帧、插入静音0、重锚2次；测试全程应用增益0。首次实时欠载方案把合成静音推进媒体时间线导致时序阶段失败，改为不伪造实时媒体帧后复测通过；首次物理音频断言因测试没有视频锚点误判renderer未运行，改用显式音频时钟后通过。日志/证据目录为`logs/audio-artifact-repair-20260914*`，完整记录见`docs/CAPTURE_AUDIO_ARTIFACT_REPAIR_2026-09-14.md`。

未修改NVIDIA/NGX运行时，未执行RTX runtime Create/Evaluate；未发布、未push、未替换正式包。真实反馈者采集卡型号、驱动与未静音听测仍待验收，不能宣称所有设备零欠载或问题已根治。下一步唯一任务：让反馈者使用本地修复版复现并回传`capture-audio-format`、`live-audio-sync`和`capture-audio-underrun`日志及听感时间点。

## 2026-09-14 用户 MOV 黑屏：负 AAC 起始 PTS 修复（本地）

用户提供 `C:/Users/123/Videos/2026-08-11 21-29-44.mov`，反馈打开黑屏。文件 SHA256 为 `4D826CE4487F19A43375DC2BD8C4A0221926B5A9C29CD224E55FA9B6BFFEAC0A`，容器为 QuickTime/MOV，视频 H.264 High 2940x1912 60fps yuv420p，音频 AAC-LC 2ch 48kHz。视频-only 无损去音轨副本可以正常呈现，原文件软件解码和 D3D12VA 探针也分别 PASS，故排除 MOV/H.264 解码、硬解纹理导入和颜色初始化；原文件全播放器复现的黑屏只在带 AAC 音轨路径出现。

根因是 `AudioPipeline::runOnAudioThread` 将 `headPtsMs()` 的所有负值都当成“没有可用音频”，而该 MOV 的 AAC 编码首帧合法起始 PTS 为 `-1.3ms`（编码器 priming）。音频 endpoint 因此只打开未锚定，音频主时钟没有启动，文件调度一直没有进入首帧呈现。`src/sink/WasapiAudioSink.cpp` 现在以实际预填充时长区分“空队列”与合法负 PTS，仅在 `prefetchedMs>0` 且 PTS 有限时锚定 renderer；`clockExhausted` 同步按预填充是否为空判断。未改变音频时间线、补偿、重采样或无音频文件路径。

修复后重建 `out/media-codec-dav1d`（`scripts/build.ps1 ... -FfmpegRoot C:/veyra-deps/ffmpeg-ps5-dav1d-installed`，44/44 增量步骤成功，最终 build exit 0）。原始 MOV 全播放器无增强 10 秒测试 exit 0，`renderer ANCHORED ptsMs=-1.3` 后释放保持，`smoke frames=542 generated=0 failed=false`，`realPresented=540`、`presentSubmitFps=60.00`；证据 `out/mov-audio-fix.stdout.log`，FFmpeg 的 `UDTA parsing failed retrying raw` 仍为可恢复元数据警告，AAC 仍有 skipped-samples 时间戳警告但不再阻止播放。软件 H.264 30帧媒体探针、D3D12VA/共享设备 12帧探针、NGX NR Create/Evaluate/Release（0x1、SEH0）、`veyra_source_tests` 及 `veyra_audio_timeline_tests`（完整实时/WASAPI/恢复/欠速用例）均 exit 0。未改 GitHub Release、未替换正式包、未执行用户肉眼画质验收。

## 2026-09-14 采集音频尖锐瞬态砂砾声：重采样过冲修复（二次施工）

用户补充同一设备在 OBS 无沙沙声、Veyra 只在尖锐声音上出现。新增尖锐阶跃与自动补偿瞬态夹具后复现了内部原因：libswresample 默认 Kaiser 在 44.1→48 kHz 瞬态中产生峰值 1.05091，Veyra 的硬裁剪记录30个 clipped；输入本来就是48 kHz时，`swr_set_compensation` 启动有效变速后同样产生峰值1.13351、32个 clipped。故不是继续泛化为“采集卡本底噪声”。

`src/sink/CaptureAudioSession.cpp` 现在为实时采集的所有 `SwrContext` 设置 `SWR_FILTER_TYPE_CUBIC`，包括48 kHz后续进入漂移补偿的路径，并记录 `capture-audio-resampler` 的输入/输出采样率、滤波器和 `compensationSafe=1`。测试 `veyra_capture_audio_tests.exe --transient` 与 `--transient-comp` 均 exit0：峰值0.999969、clipped=0、nonFinite=0。新增这一轮不是把诊断计数清零，而是先在真实转换输出上消除触发过冲的滤波器。

最终构建命令：`scripts/build.ps1 -Preset x64-release -BuildDirectory out/build/audio-artifact-repair-20260914 -FfmpegRoot C:\veyra-deps\ffmpeg-ps5-dav1d-installed`，29/29、exit0。最终 `veyra_capture_audio_tests.exe`、`--jitter`（p95 skew 20.6725ms、clipped0）、`--jitter --5.1`（p95 skew 20.7548ms、clipped0）、`veyra_multichannel_tests.exe`（31/31）、`veyra_audio_timeline_tests.exe --jitter`（1x/2x/4x）均 exit0。最终本机 USB3 实卡选择48k/16-bit/16 valid bits，收到32块，peak0.05359，欠载1次/576帧，软件插入静音0，重锚2次；增益为0，未作扬声器听感宣称。独立WASAPI harness为48k/2ch/32-bit，4秒0 underrun、drift0.01ms、Stop+Reset padding=0，exit0。

本轮修改与证据详见 `docs/CAPTURE_AUDIO_ARTIFACT_REPAIR_2026-09-14.md`。当前构建路径为 `out/build/audio-artifact-repair-20260914/veyra.exe`；未修改NVIDIA/NGX运行时，未执行RTX runtime Create/Evaluate，未发布、未push、未替换正式包。仍需反馈者在该构建上复测并回传听感时间点及 `capture-audio-resampler`、`capture-audio-samples`、`live-audio-sync` 日志；若新版仍有声音，再按实际采样率/补偿/欠载数据排查第二条路径。

## 2026-09-15 用户复测否定 cubic：撤回并改为无低通的峰值保护

用户明确反馈 cubic 版声音变闷、沙沙声反而更明显。上一条“cubic 已修复”结论撤回：测试中的 `clipped=0` 仅证明过冲被滤波器压掉，不能证明保真或听感正确。

已从 `src/sink/CaptureAudioSession.cpp` 移除强制 cubic 和 `libavutil/opt.h`，恢复默认 Kaiser/48 kHz identity 路径；对重采样转换块只做共享线性增益峰值保护（ceiling 0.995、释放过程），不再逐样本硬裁剪、不用低通。新增 `peakProtectedSamples`，并对保护日志限频；UI 显示“输入峰值 / 保护 / 异常”。这条实现的意图是保留高频与瞬态，只处理过滤器过冲造成的满幅削波。

实际命令 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "C:\Users\123\Desktop\Veyra DLSS Video Player" -Preset x64-release -BuildDirectory "out/build/audio-artifact-repair-20260914" -FfmpegRoot "C:\veyra-deps\ffmpeg-ps5-dav1d-installed"` exit 0；串行 `--transient`、`--transient-comp`、`--jitter`、`--jitter --5.1` 均 exit 0，分别确认过冲块实际触发保护而 `clipped=0`，抖动 p95 为 20.4106/20.3669ms；`veyra_multichannel_tests.exe` 为 `checks=31 failures=0`；本机 `capture:0:0:0:0` exit 0，实际选择 48k/16/16，测试增益0。`git diff --check` 仅有既存 CRLF 转换提示，无空白错误。

当前仍未完成用户反馈者设备的未静音听感验收，不能宣称沙沙声已根治；未修改 NVIDIA/NGX runtime，未执行 RTX runtime Create/Evaluate，未发布或 push。

## 2026-09-15 进一步修正：48 kHz 无补偿真正走 identity，修复 SwrContext 重建所有权

继续审查发现旧 `clearCorrection()` 调用 `swr_set_compensation(swr,0,0)` 会把同速 48 kHz context 变成强制 resample；当前以 `compensationActive` 区分无补偿与补偿，补偿结束/重锚时新建默认 context，通过原 RAII owner 接管，避免无必要的高频处理。一次复测发现新 context 替换后旧智能指针悬挂，已修复后再测。

完整构建 `scripts/build.ps1 -Root "C:\Users\123\Desktop\Veyra DLSS Video Player" -Preset x64-release -BuildDirectory "out/build/audio-artifact-repair-20260914" -FfmpegRoot "C:\veyra-deps\ffmpeg-ps5-dav1d-installed"` 为 29/29、exit0。最终串行回归：`--transient` peak1.05091/protected9582/clipped0；`--transient-comp` peak1.13518/protected9570/clipped0，重锚日志为 `native-rate path restored`；`--jitter` p95 20.7114ms；`--jitter --5.1` p95 20.4901ms；多声道31/31；音频时间线抖动1x/2x/4x；本机 `capture:0:0:0:0` exit0，48k/16/16、31块、欠载1/576帧、增益0。当前 EXE SHA256 `A98B657FA5F66F5C2A3CD26ADBFE0051DDB7C8EA7DCFC7F5D8907909D57EA7FA`，大小 3,375,616 bytes。未执行 RTX runtime Create/Evaluate，未发布或 push，用户反馈设备未静音听感待验收。

## 2026-09-15 火堆/口哨持续沙沙声：重新审查（诊断与方案，非新修复交付）

用户继续否定当前候选听感并要求审查Luna改动、给出解决方案。本轮保留全部已有产品改动和EXE，未构建/替换产品；新增 `scripts/diagnostics/audio-waveform-audit.py`、`docs/CAPTURE_AUDIO_WAVEFORM_REPAIR_PLAN_2026-09-15.md`，在旧修复文档顶部标明历史结论的证据限制。

最新真实使用会话 `logs/veyra-app.log` 第26036行起，00:09–00:24本地时间共448条同步状态，保护/削波/欠载计数均为0，最终转换后峰值0.95073；只有5次早期native context重建，之后仍持续非零时钟补偿。故过载、误淡入和少数重建不能直接解释持续噪声，不能再次强行认定根因。代码确有三项缺陷：块增益跳变且释放可再次削波、补偿归零销毁待输出滤波历史、空pull不看有效端点padding就触发5ms淡入。另有测试未检查波形、transient无视频锚点未启动输出、inputPeak其实为转换后峰值等证据缺口。

实际执行 `python scripts/diagnostics/audio-waveform-audit.py --dll-dir out/build/audio-artifact-repair-20260914 > logs/audio-waveform-audit-20260915/offline-dsp.json`，exit0。使用该应用真实FFmpeg9.0.1 DLL，hash与patched prefix匹配。0.5幅值4kHz、-1250ppm补偿归零时，对比保留context与销毁重建，后者最终少17输出帧，边界后48帧最大差0.871988；固定补偿1/4/8/16kHz拟合实际频率后残差约-100.53/-100.96/-105.82/-112.71dBFS，同速归一化误差0。最初按理论频率拟合误将整数步长频偏计为较高残差，已修正分析；不能据此说库有高频噪声。峰值保护的算术模型中，相同1.05峰值的第二块保护后仍1.0475再触发裁剪；淡入模型展示无真实断音也可能令首样本增益下降约47.6dB。这两个为明确标注的算术模型，不冒充生产集成。

完整实施/回归方案见新PLAN：先在原始PCM、重采样后、最终WASAPI提交前做有界受控tap和同源重放；去除新增失真、保持连续重采样历史，再按证据分离固定音画延迟与设备频差，不永久关闭同步或低通压噪。保留设备枚举、validBits、预填、视频reset隔离和MOV负PTS修复。下一条唯一任务P0生产波形定位。未执行产品构建/实卡听测/loopback/RTX Create或Evaluate；未修改运行组件，未push、未发布，沙沙声仍未通过用户验收。

## 2026-09-15 用户要求先修已知音频缺陷：连续历史、浮点余量与空拉恢复

本轮实施前述已确认缺陷，不盲改动态同步控制器、不加低通或降噪。修改 `CaptureAudioSession.cpp`、`WasapiAudioSink.cpp`及相应头文件、`LiveStatusPanel.h`、`CMakeLists.txt`；新增共享 `CaptureAudioDsp.h/.cpp`、生产DSP波形/真实WASAPI用例 `AudioWaveformTests.cpp`、串行限时回归脚本 `scripts/diagnostics/test-audio-continuity.ps1`；更新 `CaptureAudioTests.cpp` 的瞬态余量断言，并修复“模式切换掩盖断流恢复”的旧测试。方案完整记录为 `docs/CAPTURE_AUDIO_WAVEFORM_REPAIR_PLAN_2026-09-15.md` §6。其他原有未提交改动全部保留。

核心修改：移除块级峰值保护及转换阶段的有限浮点硬裁剪，NaN/Inf防护保留，转换后峰值和超unity计数分别记录；auto在预填前准备重采样，同一epoch归零只更新补偿，不换context或丢滤波历史；空pull仍有设备排队PCM时不触发淡入。第一次实现只以设备时钟超过提交尾部判gap，首套11组和两组120秒测试虽通过，但审查真实断供日志发现本机时钟在无数据时会停在尾部；新增真实80ms断供负向用例，明确复现 `emptyPulls=8 gaps=0 fades=0` 失败。后增加持续空端点观测满实际device period的保守路径，断流时长未知单列clockStalledGaps而不伪造missingFrames，持续恢复观察同时考虑空pull；修后同用例 `gaps=1 fades=1`，已有队列用例仍 `gaps=0 fades=0`。捕获基线现在在不改同步模式、尚未恢复输入时就验证已重锚，通过。

构建到全新 `out/build/audio-continuity-repair-20260915`，未覆盖旧 `audio-artifact-repair-20260914/veyra.exe`（原SHA仍A98B657F…EA7FA）。先完成本地音频配置，再沿用现有RemotePlay依赖配置完整构建326/326、最终行为增量34/34成功，最终 `VEYRA_ENABLE_REMOTEPLAY=ON`，避免前序音频候选关闭PS5模块；FFmpeg仍 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed`。完整 `scripts/build.ps1` 参数在PLAN §6，最终 `cmd.exe /c out\build\veyra-build-x64-release.cmd` 日志 `logs/audio-continuity-repair-20260915/build-verified.log`。首轮测试缺 `<string>` 导致C2039，补齐后成功；一次cmd正斜杠路径被拒绝，改反斜杠后正常。未隐藏失败日志。

最终短套命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915 -LogDirectory logs/audio-continuity-repair-20260915/final`，12组进程均exit0，结果 `final/results.json`。其中生产DSP47/47，44.1/48kHz、2/6声道、1/4/8/16kHz、补偿正零负和480/127帧分块均与连续参考逐样本误差0、帧数一致；故障对照重置历史后由96001帧变为95935，能检出。其余包括真实WASAPI排队空拉和持续断流、捕获完整生命周期、立体声/5.1抖动、两类瞬态余量、端点失效恢复、多声道31/31、文件完整音频时间线与抖动。全部扬声器测试静音；无用户声学验收宣称。

最终EXE空界面启动短测：`scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra.exe -Arguments @('--smoke-empty','--smoke-seconds','2','--no-nr','--no-sr','--no-fg') -TimeoutSeconds 30 -LogPrefix logs/audio-continuity-repair-20260915/final/gui-smoke`，独立 `VEYRA_LOG_FILE`，exit0；`frames=0 failed=false nrEvaluated=0 nvofExecuted=0`，只证明启动，未伪报播放/增强成功。git diff --check通过（已有CRLF警告）；没有SDK/DLL/模型/PYC被Git跟踪。最终两组各120秒快慢输入时钟漂移回归进行中，收口结果随后补充。

未做：真实火堆/口哨输入与输出分段录音、用户实卡听感、RTX Create/Evaluate、全GPU delivery gate。没有修改增强算法/导出路径，故本轮以音频专项与GUI启动检查为主；不拿这些替代RTX/导出验收。未改运行组件、未push、未发布。持续沙沙声是否完全消失仍待新构建实测；若仍出现，下一条唯一任务是同源PCM逐阶段定位，不再凭音高猜过载。

最终收口：`test-audio-continuity.ps1 ... -LogDirectory logs/audio-continuity-repair-20260915/final -DriftOnly` 两组exit0（最终目录12个短用例也全0）。1.001输入120秒P95偏差4.24594ms，0.999输入120秒4.30137ms，均missing0、resets1（仅启动）、队列高水位89.6458ms，无欠载/误淡入；证据 `final/drift-results.json`、`drift-fast.stdout.log`、`drift-slow.stdout.log`。这是合成采集节奏与真实WASAPI的专项回归，不是实卡听感。最终EXE 11,442,688字节，SHA256 `F26C655DDF7D9CF07FBA35AEA323059B9F65708139AB580015CF788B23D1D552`，路径 `out/build/audio-continuity-repair-20260915/veyra.exe`；旧候选hash未变。最终GUI空界面启动exit0、47项DSP检查通过、git diff --check通过；旧审查产生的单个PYC缓存已移除，未删用户数据。下一步用户复测新构建的真实火堆/口哨；未验收前不宣称沙沙声已根治。

## 2026-09-15 用户授权本地合并与工作区收口

用户要求“先合并一下，我需要一个干净的工作区”。确认本地 main（edabd3c）是当前修复分支（ebba6f1）的祖先；当前分支已包含媒体兼容、MOV负PTS、采集音频设备选择、FG时钟及此前HDR/多声道等改动。执行范围为提交现有23个源码/测试/诊断脚本/文档文件，然后以 `git switch main`、`git merge --ff-only codex/capture-fg-clock-repair-20260914` 收口，不另造冲突合并，不推送或发布。旧源码归档及已被取代的Smooth Motion实验分支不合入，所有原分支保留。

合并前重新执行 `cmd.exe /c out\build\veyra-build-x64-release.cmd`：exit0，配置生成成功，ninja no work to do；保留RemotePlay ON与patched FFmpeg/dav1d路径。裸 `cmake --version` 因当前PATH未配置失败，改用已有脚本中的VS CMake绝对路径，实际版本3.31.6-msvc6；可选依赖与CMake策略警告不影响构建。`out/build/audio-continuity-repair-20260915/veyra_audio_waveform_tests.exe --offline`：exit0，checks=47 failures=0，输出位于本任务命令记录；前轮完整日志继续保留在 `logs/audio-continuity-repair-20260915/final/`，不冒充本轮重新执行。EXE hash仍为F26C655D…D1D552。`git diff --check` 无空白错误，仅已有CRLF转换提示。

仅将审查过的源码、文档及脚本加入暂存；EXE、运行组件、日志与测试媒体仍被忽略且保留，不以删除本地文件制造干净工作区。当前验收目标是本地main包含修复提交、Git未提交/未跟踪状态为空；不改变真实沙沙声尚未听感验收的结论。本轮未执行RTX Create/Evaluate、GPU delivery或实卡测试，未重新跑120秒漂移用例。下一步仍是取得真实问题场景同源波形证据，用户当前远程不便测试，不要求立即验收。

## 2026-09-15 合并后项目文档同步

本地快进合并已完成，代码基线 `dc44c48`，合并后工作区干净。用户要求更新项目文档；本轮更新中英文README开发状态、`docs/BUILD.md`候选构建与专项命令，新增 `docs/LOCAL_INTEGRATION_STATUS_2026-09-15.md` 汇总分支、提交、已发布/未发布边界、候选身份与测试证据。未修改历史1.2.0发布说明、版本号、代码或运行组件。音频沙沙声仍未听感验收、生产链完整分段tap尚未实现，均明确保留，不将旧cubic/AGC施工记录误报为成功修复。

检查：PowerShell扫描上述4份文档的Markdown本地文件链接，全部目标存在（不校验外部URL或页内锚点）；`git diff --check`通过，仅既存CRLF提示。新增文档引用的构建与DSP/端点/漂移结果均标明为前轮证据，本轮未重新构建或运行音频/GPU/实卡测试，未执行RTX Create/Evaluate。文档在本地main提交以保持用户要求的干净工作区，不push、不发布；下一步仍为真实问题场景同源PCM定位，用户远程期间不要求立即测试。

## 2026-09-15 用户授权加入WASAPI采集输入

用户要求把WASAPI加入采集卡音频选择。实现保持DirectShow视频路径和已有视频filter内置音频/独立DirectShow音频不变，在采集面板的同一音频列表新增 `[WASAPI]` 端点。枚举Windows活动 `eCapture` 端点，使用FriendlyName展示、endpoint ID稳定保存；默认仍为“不监听音频”，不调用默认端点、不做loopback、不自动切到麦克风。

WASAPI输入使用共享模式事件采集，按 `IAudioCaptureClient::GetBuffer` 返回的设备位置和QPC时间将PCM转换到进程steady-clock轴；静音包按帧数补零，坏时间戳继续连续帧时间线并记估计，设备位置跳变/数据断点触发音频epoch reset。PCM进入既有 `CaptureAudioSession`，复用采样率、声道、重采样、音量、补偿和输出。设备失效只重试原endpoint三次，停止可中断等待；失败保留视频并写HRESULT，不回退默认设备。

连接串：稳定 `capture2:` 的音频模式 `-3` 加编码endpoint ID；旧 `capture:`、内置音频和DirectShow独立音频保持兼容。新增 `WasapiAudioInput.h/.cpp`、WASAPI输入测试和 `CaptureSourceTests --wasapi`；UI标签及 `--list`输出注明来源。计划/边界见 `docs/WASAPI_CAPTURE_INPUT_PLAN_2026-09-15.md`。

实际验证：

- `cmd.exe /c out\build\veyra-build-x64-release.cmd`：最终exit0，`[30/30] Linking CXX executable veyra.exe`。首次因Windows `cguid.h`包含顺序失败（C2059 `__uuidof`），调整头文件顺序后重建通过；失败日志保留。
- `veyra_wasapi_input_tests.exe --offline`：最终连接串、QPC时间轴、坏时间戳、静音/异常包等 **18项**通过。
- `veyra_wasapi_input_tests.exe --invalid`：无效endpoint不回退、重试上限3次、停止打断退避，exit0。
- 本机显式USB3 Digital Audio endpoint 3秒输入：共享48kHz/32-bit float，超过20个PCM块，停止/重开通过；日志/输出在 `logs/wasapi-input-20260915/`。注入设备失效后只重连同endpoint的测试通过。
- `veyra_capture_tests.exe --wasapi <本机显式endpoint ID>`：视频+WASAPI音频组合通过；同一设备的原DirectShow路径也通过。测试增益为0且不保存PCM，不能替代用户听感。
- 旧 `test-audio-continuity.ps1` 12组音频专项此前已通过；本轮补充脚本中的WASAPI离线时钟项，未重复声学/GPU验收。

候选仍是本地 `out/build/audio-continuity-repair-20260915/veyra.exe`，未更新版本号、未修改运行组件、未push、未发布。WASAPI接入已具备本机候选证据，但反馈者设备的端点是否提供正确采集音频、以及火堆/口哨沙沙声是否改善，均未验收。

## 2026-09-15 采集自动同步异常补偿修复

用户授权修复采集自动同步可能累积数秒声音等待的问题。新增 `CaptureSyncTarget.h`，以同一原始视频帧的到达至呈现时间核对跨音视频时间戳；差异超过80ms时回退本机延迟估算，连续2秒恢复可信才退出回退。80ms是可信度策略，不是硬件实测；真实上游偏移可能需要手动校准。CaptureCardSource、WASAPI输入适配和EngineController传递匹配帧的到达时刻，排除生成/缓存帧；CaptureAudioSession记录原始、接受及回退目标，LiveStatusPanel显示异常回退。PS5无到达时刻的旧接口与文件音频保持原路径。

专项测试另发现慢启动时原始PCM积压未纳入过期处理。修复在输出启动/重新对齐前转换有界待处理输入，让已有恢复策略清理过期声音；正常运行的填充策略保持。新增恢复丢弃计数及650ms慢启动注入。修复前合成慢启动P95偏差646.558ms、额外重置4次，修复后专测20.1321ms、额外重置/欠载0；移除启动时648.396ms过期PCM。最终16组套件中的慢启动P95为15.2911ms。时间戳单独偏移1200ms用例的目标从修复前1235.51ms降为35ms，软件队列从1214.56ms降为10ms；真实400/900ms本机处理等待仍保留。

实际命令及结果：

- `cmd.exe /c out\build\veyra-build-x64-release.cmd`：最终构建成功，增量4/4，RemotePlay ON及patched FFmpeg/dav1d保持；日志 `logs/capture-sync-repair-20260915/build-final.log`。中途测试引用未声明kAudioRate导致两次编译失败，改为测试固定48kHz下的24000帧阈值后通过。
- `out/build/audio-continuity-repair-20260915/veyra_repair_contract_tests.exe`：123项，失败0；`logs/capture-sync-repair-20260915/contracts.log`。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915 -LogDirectory logs/capture-sync-repair-20260915/final`：16组全部exit0，含立体声/5.1、端点恢复、时间戳异常、慢启动、手动/关闭补偿及文件时间线；证据 `final/results.json`。
- 上述命令增加 `-DriftOnly`：两组120秒全部exit0。1.001与0.999输入速度的P95偏差分别4.22906/4.23319ms，missing=0、resets=1（仅启动）、队列高水位89.6458ms；`final/drift-results.json`及对应stdout日志。
- `scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra.exe -Arguments @('--smoke-empty','--smoke-seconds','2','--no-nr','--no-sr','--no-fg') -TimeoutSeconds 30 -LogPrefix logs/capture-sync-repair-20260915/gui-smoke`：空界面启动exit0，处理帧0，不代表媒体或增强验收。`git diff --check`通过；源码Git未跟踪DLL/LIB/EXE/模型/PYC。

当前候选EXE为11,463,168字节，SHA256 `F060238E39311D9E2D3D8B8CB4EA50B0CBB6E4BD24E67BC10D90A249F417BACC`。构建目录仍为 `out/build/audio-continuity-repair-20260915/`；根目录旧启动器未调整。本轮未发布、未修改运行组件或版本号。完整方案、修改文件与证据见 `docs/CAPTURE_VRR_AUDIO_SYNC_AUDIT_2026-09-15.md`。

测试使用合成采集时序与真实WASAPI静音输出，未执行反馈者实卡、VRR开关对照、声音录制或RTX Create/Evaluate/GPU delivery。软件缺陷有复现及修复证据，但不能认定VRR为反馈者根因。下一步唯一验收：反馈者同一采集卡/场景开启自动同步，对照VRR开关并提供新目标/队列日志。

## 2026-09-15 用户集中反馈总修复方案

用户汇总14项问题：采集卡选择记忆、播放器seek/流畅性、UI闪烁、采集断连重连、全屏控制、HDR/SDR发灰、NR内部处理分辨率、OBS resize回归、Dolby Vision、FG后端失败、XeSS计时、同步/增强冗余、40系过载、视频导出队列与BT.2020错误、PS5串流增强初始化失败。已建立 `docs/USER_ISSUES_REPAIR_PLAN_2026-09-15.md`，按“不可用恢复→颜色→交互→性能→导出→Dolby Vision”分批施工，记录每项证据、验收合同和未验证边界。本轮仅建立方案，未修改代码、未构建、未发布；后续按批次逐项更新实际结果，不能把方案文档当作修复完成证明。

## 2026-09-15 集中反馈修复本地候选

用户授权目标模式、独立分支和全部修复；从 `6206b5a` 建立 `codex/user-issues-repair-20260915`。保留此前采集同步和音频连续性修复，本次交付为可回退的本地候选，不等同14项已在反馈者硬件全部验收。逐项实现、证据及剩余项见 `docs/USER_ISSUES_REPAIR_PLAN_2026-09-15.md` 第8至10节。

修改范围：`CapturePanel`/新增 `CapturePreferenceStore` 保存稳定设备及格式选择；`CaptureCardSource` 和 `WasapiAudioInput` 同设备退避恢复；`AppShell` 修正视频GDI重绘及方向键seek；`EnhancementSettings`/`ResolutionPlan`/设置UI增加480/720/900/1440档。`MediaFileSource` 清理过期seek结果并记录首次呈现时间，新增 `DolbyVision.h` 区分配置及兼容基础层。颜色元数据和三个输入shader补显示参考BT.709及SDR BT.2020 NCL；`VideoExportJob` 使用实际首帧颜色合同。`PresentSink` 接受合法flip模式替代并保留暂时resize失败时的缓冲；`EngineController`/`EnhanceGraph`/新增 `BackendRecovery` 分组件恢复，XeSS呈现失败时重建调度器和交换链。GPU计时按外层循环消费，FG恢复预算使用同帧实际GPU执行区间。对应单元、UI、GPU颜色、呈现及输入测试一并更新。

实际验证命令与结果：

- `cmd.exe /c out\build\veyra-build-x64-release.cmd`，最终exit0，日志 `logs/user-issues-build-20260915-k.log`；构建目录 `out/build/audio-continuity-repair-20260915`，RemotePlay ON，FFmpeg仍为 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed`，保留PS5 slice补丁。
- 构建目录下 `veyra_repair_contract_tests.exe`：137项失败0；`veyra_ui_contract_tests.exe <独立输出目录>`、`veyra_wasapi_input_tests.exe --invalid`、`veyra_source_fidelity_tests.exe` 均exit0。证据为 `logs/user-issues-contract-i.log`、`user-issues-ui-j.log`、`user-issues-wasapi-invalid-i.log`、`user-issues-resize-i.log`。普通/捕获兼容模式下同窗口反复1920/2560/3840 resize有真实GPU读回检查。
- 经 `scripts/run-short-test.ps1` 限制180秒执行 `veyra_live_presentation_tests.exe <用户4K文件> <独立日志目录> --backend-recovery`：12项通过，`logs/user-issues-backend-recovery-k.stdout.log` 和 `logs/user-issues-backend-recovery-k/engine.log`。真实NR/XeSS启用、注入初始化/运行/呈现故障、恢复基础播放、手动重启XeSS同会话成功。NGX Init/CreateFeature18返回 `0x1 Success`，NVOF Create状态0；注入故障不冒充用户实卡故障复现。
- 同一呈现测试的 `--seek-stress`：用户4K视频6次远距跳转首次呈现157至339ms，暂停最新目标约222ms；`logs/user-issues-seek-j.log` 及同名目录。无修前对照，不宣称PotPlayer级速度。`--file-fg-recovery` 注入70ms主循环阻塞后恢复生成，约13→46fps，生成约19fps，`logs/user-issues-fg-recovery-h.log`；不是稳定60fps或RTX40实卡证明。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915`：最终exit0，47.98秒，`logs/delivery/23d9cf72ba7a463aacda42f56be591e3/result.json`。实际1080/原生4K NR/NVOF、GUI播放/暂停seek、图片、H.264/HEVC原生4K 2X完整帧数与音轨、取消检查通过；实卡采集明确待验收。
- HDR GPU颜色检查见 `logs/user-issues-hdr-color-20260915.log`。另经短测脚本60秒上限执行 `veyra.exe out/hdr-audio-fixtures/pq-tagged-51.mp4 --no-nr --no-sr --no-fg --hevc --export-out logs/user-issues-hdr-export-k.mp4`：exit0；已安装外部ffprobe完整计数得到HEVC Main10/yuv420p10le/BT.2020 NCL/PQ/30帧、AAC 6声道5.1。日志 `logs/user-issues-hdr-export-k.stdout.log` 与 `logs/user-issues-hdr-export-k-app.log`。

中途失败如实保留：ResetReason枚举及缺少标准头导致编译失败后修正；backend-recovery-i因测试参数白名单遗漏exit2，修正后j/k通过。首次ffprobe路径不存在，改用已安装Gyan CLI完成检查。修改后重新跑相关恢复及delivery，没有把失败当成功。

候选入口 `out/start-user-issues-candidate.cmd`，实际EXE为 `out/build/audio-continuity-repair-20260915/veyra.exe`，SHA256 `9DC754D09BB3835E605D32A8825040DFD25A24669D81789B8924D47351EEBAD3`。根目录旧 `Veyra.cmd` 未改，不代表桌面原入口已更新。运行组件/SDK/配置/测试媒体/日志保持本地忽略，不加入提交；本轮不升版本、不push、不发布、不关机。

尚未交付或验收：原生Dolby Vision RPU/增强层及输出、真实DV素材；XeSS SDK内部精确GPU时间；DirectShow音频pin单独初始化失败后的独立热重建；独立预解码worker及PotPlayer速度对照。物理采集卡拔插、4070/4070Ti、OBS真实hook、PS5重连和全屏方向键人工实操未执行。不能声称全部用户发灰、过载及导出异常已经根治；下一步为候选在问题设备上的复测和原始日志核对。

## 2026-09-15 同视频无增强颜色偏暗：推翻旧预期并修复

用户截图反馈普通播放器与Veyra无增强画面颜色不同，要求从链路定位而非提高对比度。确认用户运行本地候选，原媒体为Downloads内 `OpenAI-This_is_GPT-6_Astra__10368kbps-20260906113552.mp4`，1080p H.264/yuv420p、未标记颜色参数。生产推定limited/BT.709。发现上一批文件入口使用pow2.4（BT.1886理想黑点），输出仍sRGB，两曲线不互逆而压暗。原SourceFidelity期望值也执行同样曲线，错误地把算法一致性当成信号保真；撤回前文以该结果宣称文件中灰已正确修复的结论。

先将灰阶测试预期改成独立的limited→full代码值恒等关系，旧版exit1；新增 `FileSdrRoundTripCases.h` 用生产MediaFileSource软/硬解同一文件60秒后的同帧，独立YUV矩阵计算RGB预期并读回图输出和真正呈现缓冲。修前软/硬解同PTS513137900：平均误差6.74098级、最大13.3126级且全部偏暗；图至呈现误差0。修后平均0.256191、最大0.629614、平均偏差-0.0670757级；灰阶最大0.575342级、呈现误差0，exit0。证据 `logs/sdr-file-before-20260915.stdout.log` 与 `logs/sdr-file-after-20260915.stdout.log`，不将诊断GPU回读加入生产路径。

实现 `ColorDescription::preserveSdrCodeValues`，文件和采集用与现有sRGB输出互逆的工作解码，同时保留源transfer元数据。修正采集显式BT.709错误分支，设备格式匹配比较包含新策略；日志补充实际工作transfer。PS5旧参考显示策略及PQ/HLG路径未改，不拿本例判断它们是否正确。完整根因和边界见 `docs/SDR_CODE_VALUE_ROUNDTRIP_REPAIR_2026-09-15.md`。

实际构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0（75/75），`logs/sdr-roundtrip-build-20260915.log`。用户明确关闭软件后替换候选EXE。`veyra_source_fidelity_tests.exe <用户文件>` 经90秒短测限制exit0；`veyra_hdr_color_tests.exe` 同样限90秒exit0，涵盖HDR/SDR/采集GPU色块；`veyra_repair_contract_tests.exe` 137项失败0；`veyra_capture_color_tests.exe` 限30秒exit0。首次误拼为capture_color_contract_tests导致命令未执行，之后按CMake真实名称纠正；不采用那次残留LASTEXITCODE。

再执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915` exit0，结果 `logs/delivery/3c32cadc5f774f6eaae43efee5d02211/result.json`，真实NR/NVOF、4K导出等短测通过；不能替代问题设备实测。EXE SHA256 `EFA748C7F7D64DF3E9ECE814CE56D2015BC976192F00195443C2B0E17A8EDA96`，入口 `out/start-user-issues-candidate.cmd`。git diff --check通过；版本、runtime及发布状态不变。

下一步唯一用户验收：同原视频、关闭所有效果对照新候选。未核对其他播放器ICC/驱动视频增强/显示设置，不能宣称所有播放器屏幕像素绝对相同，也不能将本次SDR修复扩大为全部HDR发灰根治。该修复与此前14项候选一并作为当前独立分支的本地可回退记录，原基线6206b5a保留，不push、不发布。

## 2026-09-15 HDR发灰端到端排查

用户要求继续排查HDR，不知道反馈时实际是原生HDR还是转SDR。沿用 `8812e94` 在独立分支施工。新增 `HdrNativeRoundTripCases.h`，16组覆盖PQ/HLG、full/limited、平面10bit/P010、FP16 scRGB/RGB10 PQ，包含近黑、彩色、宽色域与最高10000尼特参考信号。通过 `VideoPresenter::presentedResourceForTest` 读取真实交换链缓冲作数值对照；该接口只供测试，无生产回读。PQ→scRGB最大相对误差0.0868%，PQ→RGB10 0.524%，HLG分别0.0893%/0.648%，图输出与呈现缓冲精确一致。证据 `logs/hdr-native-hlg-audit-20260915.stdout.log`，90秒上限exit0。

增强短测：`veyra_hdr_enhancement_tests.exe 1` exit0、NR20帧、高光1003.75尼特；`... 3` 最终日志pass1、NR20/SR20/生成18、高光998.932尼特，用户中断后工具句柄消失，确认进程退出并读取最终日志，没有虚报无法回取的进程exit码。`... 1 0 out/hdr-audio-fixtures/pq-tagged-51.mp4 1` 90秒上限exit0、实际硬解文件及NR20帧。分别见 `logs/hdr-audit-nr-20260915.stdout.log`、`hdr-audit-sr-fg-20260915.stdout.log`、`hdr-audit-file-hardware-20260915.stdout.log`。NGX执行及基底身份、JXR、有限高光检查真实通过；本地fixture不是反馈者电影，不代表完整实际画质验收。

确认HDR→SDR使用固定1000尼特/203参考白肩部和简单色域裁切，未依据内容峰值调整；这是已有策略的局限，不能直接判成全部HDR发灰根因。原生HDR没有复现SDR先前的BT.1886/sRGB曲线错误。本轮不调整HDR曲线或对比度。新增 `MediaFileSource` 首帧母版亮度/MaxCLL/MaxFALL来源日志，未知-1；`EnhanceGraph` 记录实际HDR输入、工作单位、输出和tone-map策略，不让通用SDR workingTransfer字段误导HDR诊断。

初次新增测试引用不存在的makeReadbackBuffer构建失败后修正；固定0.1尼特中性容差在10000尼特FP16下超出格式精度，改按相对精度阈值，原失败日志保留。完整 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0（38/38），日志 `logs/hdr-audit-product-build-20260915.log`。最终GUI无增强HDR文件3秒短测，30秒上限exit0，`logs/hdr-audit-gui-20260915.log`。仅诊断和测试接口变更，未重跑完整delivery；git diff --check通过。

候选EXE SHA256 `1CAC34939919C19EEF1FF271857159E8786AD2EF1BA7FCA5CBD6EBE394DC6321`，原候选入口保持。完整结论及后续有证据再改映射的方案见 `docs/HDR_GRAY_CHAIN_AUDIT_2026-09-15.md`。下一条唯一任务：取得问题HDR片段和新日志，依据hdr-route区分原生HDR/转SDR，量化同帧变化再修映射，不凭主观发灰统一改gamma。未执行反馈者屏幕测光、实卡/PS5或未知HDR素材，未发布或变更运行组件。

## 2026-09-15 全屏交互补修与14项对抗式复核

用户反馈全屏不能拖动和方向键无效。使用computer-use原生输入在旧候选实际拖动，画面仍停留原时间附近；确认AppShell布局把playbackBar提到seekBar前、焦点留在滑块时快捷键被拦截。修改 `apps/veyra/ui/AppShell.cpp`：seekBar置前、全屏20 DIP命中区、进入/离开全屏及点击视频重新取得播放焦点、全屏左右键不受音量滑块阻拦但保留编辑框/弹出选择器边界；取消捕获后清除dragging，自动隐藏区域包含进度条上缘。`Theme.h`滑条鼠标按下取得焦点。新增 `TransportChecks.h` 经 `--smoke-transport` 验证真实HWND命中与消息队列。

构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` 最终exit0，`logs/fullscreen-transport-build-b-20260915.log`；初次LONG/int初始化列表不一致导致编译失败后修正，保留原日志。18秒交互测试经run-short-test限制45秒，最终exit1：焦点/命中检查通过、拖动保持断言失败，此后出现非脚本发出的实际seek和方向键。测试与用户手动操作重合，但不能未经独立复核就归咎操作干扰；不声称自动交互回归通过。用户随后明确确认测试正常，记录第5项用户本机验收通过。候选hash `504E4A27DD6FC56262F97E63D9262ABBA2B68CBC9799C6B901359AC28BFEBF53`，入口保持 `out/start-user-issues-candidate.cmd`。

用户接着要求复核14项是否完成及方向错误；本轮读取采集重连、颜色、分辨率、呈现resize、FG恢复/预算、计时、导出、DV路由的生产实现及原始日志，新增 `docs/USER_ISSUES_REPAIR_AUDIT_2026-09-15.md` 并更新原计划。用户确认1/2/3/5成功；第9原生DV、第11XeSS内部计时明确未完成；其他待硬件或原文件验收项不降格成完成。再次纠正旧SDR曲线/测试预期及把计时队列当视频队列的错误解释；额外指出DirectShow音频独立恢复、Stop返回值、GPU时间戳缺失时CPU预算回退、非NVIDIA请求/实际状态核对、无FG时“补帧受限”文案等缺口，没有把风险写成已复现根因。

实际重跑：`out/build/audio-continuity-repair-20260915/veyra_repair_contract_tests.exe`，137 checks / 0 failures，exit0，`logs/user-issues-audit-contract-20260915.log`；`veyra_ui_contract_tests.exe out/user-issues-audit-ui-20260915`，exit0，384布局/DPI及设置/PCM合同通过，`logs/user-issues-audit-ui-20260915.log`。本轮审计未重跑NGX Create/Evaluate、GPU完整gate或实卡；引用历史证据均标明。未改运行组件、驱动、版本或发布状态。下一任务优先取得HDR实际路由/问题片源及4070/4070Ti后端失败和GPU预算证据；全屏自动回归需在无并行手动操作时独立复核。全部修复目标尚未完成。

## 2026-09-15 独立FG选择生效与剩余代码缺口修复

用户报告全关状态FG不生效、需先开NR，授权继续修能修的缺口。确认是SettingsWindow倍率选择在master关闭时只保存草稿，NR独立按钮开启master才带出FG；底层FG本来包含光流初始化条件，未添加隐式NR预热。FG选择接入显式开关事务，新增FgOnlyChecks经真实控件通知验证，具体根因/文件/命令/日志/限制见 docs/FG_STANDALONE_AND_RECOVERY_REPAIR_2026-09-15.md。

同步修改FgRecoveryBudget缺失GPU时间不使用CPU迟轮询值、BackendRecovery及EngineController规范非NVIDIA请求/已应用状态、LiveStatusDashboard区分无FG的处理过载。CaptureCardSource抽取并复用DirectShow音频连接函数，新增AudioInputRecovery按PCM包进度检测3秒停滞/1至5秒退避；保留视频filter，短停共享图后仅重连音频pin、恢复音量同步、增加epoch并标下一帧Discontinuity。Stop失败不继续热改图，相关HRESULT记录；最终关闭时的异常驱动行为没有因此自动得到安全证明。WASAPI原有恢复不改、不换默认设备。

构建脚本主构建79/79和最终30/30均exit0，日志fg-standalone-recovery-build[-final]-20260915.log。144项repair合同exit0（fg-recovery-contract-20260915.log），presentation_worker预算回归exit0（fg-recovery-budget-20260915.log）；首次误写不存在的测试EXE名，纠正后实际运行，不以那次未执行作为成功。FG-only GUI30秒/55秒总上限exit0，日志fg-only-selector-test-20260915.log：DLSS和XeSS实际生成，NVOF创建status0，NR评估0；XeSS Init/Present结果0持续framesPresented2。独立全屏18秒/45秒上限exit0，fullscreen-transport-isolated-20260915.log；此前与用户操作重合的失败保留。

最终delivery.ps1 exit0，logs/delivery/1987bb07e103441591d6dacf2e632cac/result.json，实际NR/NVOF、4K和GUI/导出相关检查通过；capture awaiting仍保留。候选hash 00DD5FD72B1849006F86041723B150E9EDF3D19575DFBCC5B0E47CE2BF86AD8E，入口out/start-user-issues-candidate.cmd。XeSS旧版历史9057d2f/83f90ba面板也显式不可测；当前SDK状态字段无内部耗时，用户对应旧截图/版本未知，未伪造计时。

本地按显式源文件清单提交，全屏前轮代码/审计文档一并存档；运行库、SDK、配置、日志/fixture不入Git，未push/发布。DirectShow实卡音频热恢复、非NVIDIA实机、40系/OBS/PS5原问题均未因此视为实机通过；原生DV与XeSS内部计时仍未实现，HDR转SDR没有无证据改曲线。下一步为候选问题设备复测及其原始证据定位。

## 2026-09-15 拖动实时预览、NR强度暗部与导出收尾

用户要求拖动途中出画面、检查NR全部参数及强度2暗部、调查导出99%失败和不弹保存窗口。实现及逐文件/测试细节见 `docs/SCRUB_NR_EXPORT_REPAIR_2026-09-15.md`。新增SeekPreview在UI保留单在途+最新目标，拖动临时暂停运输、显示每次已完成预览，释放优先精确定位并恢复先前播放状态。NR残差外推可能负值截黑，新增暗部切线连续正值延伸，不改输入/输出transfer和默认NR端点。旧参数测试未显式开启NR的漏洞已修，18组72次实际Evaluate；肤质/UI修正仍未证实。

VideoExportJob修正视频包失败仍计数、检查关闭刷新、细分编码尾帧/MP4索引/验证/改名失败；验证报告帧号/PTS/源错误，长验证显示已检查帧数。ExportJobManager识别细分收尾进度。导出按钮不再因failed静默返回，使用独立worker已应用设置；其他拒绝和系统保存弹窗错误明确提示。既有文件/partial保留，不弱化帧数/时间戳/EOS验证。

实际构建脚本scrub-nr-export-build系列exit0；shader45秒上限exit0、真实NR参数120秒上限exit0、最终GUI24秒/55秒上限exit0；前两次GUI测试失败及修正原因保留在对应文档。真实3600帧文件锁测试export-lock-final在全部解码通过后得到Windows32，并正确报告保存失败；export-corrupt-output破坏尾包后正确拒绝成功，两项各120秒上限exit0。computer-use实际点击导出按钮，观察另存为窗口及默认名字，取消未导出。

完整 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915` exit0：`logs/delivery/582eaf50540a4f2f82abf2b7b82b3cfc/result.json`。实际NR/NVOF、原生4K、GUI/图片、H264/HEVC4K2X导出/音轨/取消通过，采集待实卡。候选EXE SHA256 `E0FE742B3EC7B83BEA18390EA7E61472D5CBB925F8DC0A6809F925EE92DB86E9`，入口保持 `out/start-user-issues-candidate.cmd`。未改运行组件、SDK、版本或发布。反馈者99%根因尚缺原文件/日志，不能拿注入文件锁替代真实复现。后续继续独立的HDR转SDR曲线/色域修复。

## 2026-09-15 采集卡音乐滋滋声：真实播放间隙修复

用户将采集音频严重杂音提为最高优先级，并明确开启PS5音乐授权实卡录音；暂缓HDR色调映射。前置提交f7bc4eb，仍在codex/user-issues-repair-20260915。完整根因、修改清单与命令见 `docs/CAPTURE_AUDIO_BUZZ_REPAIR_2026-09-15.md`。

物理USB3 Digital Audio输入48k/16bit/2ch/10ms。原始、SWR、pull、WASAPI提交四阶段录音表明转换和队列没有引入块断裂，但自动同步负ppm耗尽20ms播放储备；实际每周期少量缺样被Windows插入短静音，驱动时钟停顿使旧underrun计数仍为0。仅指定测试Veyra进程的Windows loopback，修复前5～20秒96处明显双声道归零断口、129零frame，1902次稳态写入padding全为0；修复后同长度0断口/0零frame，1899次稳态写入无padding=0。两段音乐非逐帧相同，不以RMS或峰值作音质分数。

CaptureAudioDsp增加调度储备约束，CaptureAudioSession将输入/PCM/端点真实储备纳入速度校正，不让不可达到的更早画面目标饿死播放；保留连续SWR、原有20ms安全窗、有储备时的正常追赶。新增显式环境变量才开启的20秒有界诊断和capture-pcm-audit.py；默认不录音，不记录其他应用。WaveformTests增加反例测试。

最终构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0，`logs/audio-buzz-final-build-20260915.log`。waveform测试51/0，真实静音WASAPI capture_audio全模式/延迟/重建/恢复/容量回归exit0（各60秒上限）。实卡NR+FG运行32秒、进程回录28秒；一次修复后测试因用户另一份Veyra占用，Run=0x800705AA且录音为空，明确失败，用户关闭后after-b通过，失败记录保留。独立录音分析JSON见audio-buzz-before/after-audit-20260915，实际SDK返回值在录音应用日志及delivery原始日志。

完整delivery同上命令exit0，`logs/delivery/3c3099c2022d4272a439d63b3062363b/result.json`，46.48秒，实际NR/NVOF/原生4K/播放控制/图片/导出音轨通过。候选EXE SHA256 `912FECDF30FE385BC33F1A6272EF8DF8E695B43F1D76F650AC2B521422CEBF93`，`out/start-user-issues-candidate.cmd`。运行组件/SDK/配置/录音不提交Git，没有push或发布。本机回录消除了已复现的间隙，用户最终听感及其他卡型号尚未验收。

持续漂移 `veyra_capture_audio_tests.exe --drift-slow` 经run-short-test、150秒上限，日志 `logs/audio-buzz-drift-slow-20260915.stdout.log` exit0。实际120秒/输入慢0.1%/静音WASAPI，软件时差绝对值P95=4.279ms，missing=0、resets=1（启动）、队列高水位89.646ms、稳态underrun=0，未出现累计多秒延迟。不是声学延迟证明。下一步仅交用户试听确认本次采集杂音，不据此宣称所有卡已修好。

## 2026-09-15 HDR映射候选完成与补帧产出/提交口径排查

HDR修改ColorMetadata/FramePacket/MediaFileSource静态元数据继承，HdrToneMap源峰值选择与图内固定，YuvToLinearRgb/HdrToSdr亮度映射及同亮度色域压缩，EnhanceGraph根常量合同、CMake依赖及media_probe ABI；新增GPU颜色测试。详细命令、失败和SDK证据见 `docs/HDR_TO_SDR_MAPPING_REPAIR_2026-09-15.md`。首轮根常量参数错误导致黑输出并测试失败，已修正；file-c经150秒上限exit0，20组映射最大Y误差0.000526118、色度方向0.000390535，16组原生HDR通过；PQ2000nit软硬解一致，6组真实NR/SR/FG执行通过，NGX Init/CreateFeature 0x1 Success。此前EXE锁定链接失败和并行采集期间player-sync gate失败均保留记录，未冒充通过。

用户反馈“受限却180fps”后，原日志session3/revision556证明GPU原帧60+生成120，但显示提交141至170fps，累计2780有效生成有360过期丢弃。AppShell主标签原读outputCompletedFps，状态判定读presentSubmitFps；本轮将标签统一为显示提交，LiveStatusPanel明确非屏幕实测/原帧与生成帧提交/产出含过期。没有改丢弃阈值掩盖欠速，实际采集3X过期原因仍待逐子帧时间验证，见 `docs/FG_OUTPUT_RATE_AUDIT_2026-09-15.md`。

最终标准构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0，日志 `logs/fg-rate-display-build-20260915.log`。完整delivery命令沿用上文，exit0，`logs/delivery/6bade0b84d8449d89198cffa37453a2d/result.json`，原始应用日志 `logs/fg-rate-display-delivery-20260915.log`。实际NR/NVOF/4K/GUI/导出等通过，本次未运行用户并行采集；不冒充采集3X根治或其他硬件验收。`git diff --check` exit0，仅换行提示。

标准候选SHA256 `888BC5E1ED098AD1E3A4C0188D58100452E31364DF45826267FAADB9C01C24E6`，入口 `out/start-user-issues-candidate.cmd`。运行组件/SDK/本机媒体保持源码Git外；未发布。下一项是采集3X补帧生成后过期的调度专项，不是重复改HDR对比度。

### 2026-09-15 补帧受限与180fps口径修复

用户反馈3X补帧受限但右侧180fps。`logs/fg-deadline-baseline-capture-20260915.log` 用同一采集参数 `capture:0:0:0 --nr --video-sr 2 --fg-multiplier 3 --smoke-seconds 32` 复现：有效生成3480、生成提交3060、过期420；batch442第一张已在截止后8.546ms就绪，决定时10.588ms，因整批resolve等待后续子帧而越过10ms容差。UI原先右侧读 `outputCompletedFps`，含过期产出；已改为 `presentSubmitFps`，详细面板明确显示提交与含过期产出。

实现 `EnhanceGraph::resolveFrame`，按每个MFG子帧lease/fence独立解析有效状态；实时采集/串流按子帧就绪、PTS顺序提交，文件/导出仍使用完整 `resolveGeneration`。保留2批队列、consumer fence、原10ms过期规则和GPU资源状态，不放宽阈值或增加缓冲；不宣称实测延迟没有变化。新增 `fg-deadline`限频日志只记录CPU首次观测栅栏、截止/决定时间和是否整批ready，不冒充GPU精确终点或屏幕扫描率。

修后同参数32秒 `logs/fg-progressive-capture-20260915.log` exit0：生成3478、提交3478、过期0、显示提交180fps；120秒持续采集 `logs/fg-progressive-capture-long-20260915.log` exit0：源7062、生成14050、提交14050、过期0、显示提交180fps，`failed=false`，无错误。既有 `veyra_presentation_worker_tests.exe`、`veyra_live_timing_tests.exe` 回归exit0，日志 `logs/fg-progressive-worker-tests-20260915.log`、`logs/fg-progressive-timing-tests-20260915.log`；新增逐帧行为由实卡对照验证。最终delivery exit0，结果 `logs/delivery/90feb52b7ed84cf19d92d64596d79f10/result.json`，47.91秒；未发布。

最终候选SHA256 `02BBC51FF46A8DEBCCA9961BEC48500E2AA77FE62B238174172D4E238ACA6401`，入口沿用 `out/start-user-issues-candidate.cmd`。详细文件/命令/SDK结果及边界见FG_OUTPUT_RATE_AUDIT。回调至Present返回P95前后44.606/50.328ms，不能从丢帧改善推论屏幕延迟降低；真实屏幕与帧间均匀性尚未测量，下一步交用户同一组合游玩验收。HDR改动一同保留为本地可回退记录，运行库/SDK/媒体未入Git。
## 2026-09-15 1.3.0发布后文档对齐

## 2026-09-16 AMD FSR 帧生成接入（隔离分支 codex/framegen-fsr-dolby-20260916）

按 `docs/FRAMEGEN_FSR_DOLBY_PLAN_2026-09-16.md` 的 E 工作流施工，**只在隔离分支**，
未合并 main、未推送、未发布。详细设计、API 顺序、约束与命令清单见
`docs/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md`。

新增 `FsrFgPresenter`（FidelityFX loader + 帧生成/代理交换链上下文 + 每帧 prepare/configure/
插帧 dispatch + 提供方 present 回调计数与合成）、`PresentSink`/`VideoPresenter`/`EnhanceGraph`/
`EngineController`/UI/预设 schema v13 接入，`--fg-fsr` 开关，`tools/fsr_probe` 扩成完整流水线探针
（计数回调、upscale 几何、pipelined 模式、销毁后重建检查）。

关键实测（本机 RTX 5070 / 616.56，全部为玩家真实运行）：

- 探针：90 帧 → 89 生成帧、0 失败（`logs/fsr/probe-recreate.log`）；请求 2/3 张生成帧时提供方
  仍只给 1 张/真实帧 → **3.1.x 上限就是 2X**，故引擎与预设把 FSR 倍率门限设为 2X；
- 玩家 1080p：226 真实 / 222 生成，exit 0（`logs/fsr/smoke-fsr-final.log`）；
- 玩家 4K 渲染：render 3840×2160 → display 1280×712，559 真实 / 555 生成，exit 0
  （`logs/fsr/smoke-fsr-4k.log`）；
- 设置事务重建（开超分）后继续补帧，exit 0（`logs/fsr/smoke-fsr-rebuild2.log`）；
- 回归：DLSS 6X 875/177、XeSS 4X 639/217 均 exit 0（`logs/fsr/regress-dlss6x.log`、`regress-xess4x.log`）；
- `scripts/gates/delivery.ps1` PASS（`logs/delivery/4f387d93def9440f8fa9f7efe92d6bd3/result.json`）；
  `veyra_repair_contract_tests` 157 项 0 失败；`veyra_repair_preset_tests` 全通过
  （其中两条旧断言因 XeSS 4X 放宽而失效，已按现合同修正，不是掩盖失败）。

过程中修掉的两个真实缺陷：插帧命令列表必须在 `Configure(frameGenerationEnabled=true)` 之后查询，
否则拿到空列表导致 `ffxDispatch` 返回 `ERROR_RUNTIME_ERROR`；`EnhanceGraph::applySettings` 的
DLSSG 能力门控没有排除 FSR，导致 FSR 会话下任何就地设置变更被回滚。

**未验证/未完成**：AMD 显卡实机、HDR10 输出、采集卡实时输入下的 FSR 帧生成；FSR 4.0.1 ML 提供方
在本机 NVIDIA 上未被枚举。以上均不得当作已完成能力对外描述。

## 2026-09-16 40 系 DLSS MFG 解锁调研（未实施）

### 2026-09-16 FSR 超分可行性（N 卡）与接入点勘察

### 2026-09-16 FSR 超分接入产品（同一分支，未合并 main）

### 2026-09-16 40 系 DLSS MFG 解锁（C-2）实现 + 本机结构/补丁机制验证

### 2026-09-16 杜比/DTS 位流解码兜底（G-2）实现 + 本地解码验证

### 2026-09-16 采集音频改为可手动指定（用户反馈自动识别不好用）

用户反馈"自动识别并不好用"，要求在采集面板里像 HDR 那样手动选。已加：

- 采集面板新增下拉框「采集音频（变更需重连）」：**自动**（优先线性 PCM，PCM 不可用时位流解码）、
  **强制线性 PCM**（不接受 Dolby/DTS 位流）、**位流优先**（Dolby/DTS 直通解码为 PCM，
  适合 PS5 已设成 Dolby 输出但设备同时提供 PCM 的情况），并带与 HDR 那两项同风格的说明文本。
- 设置持久化：`EnhancementSettings::captureAudio`（`engine::CaptureAudioIngress`），
  预设 schema 升到 **v14**（旧版本读入默认"自动"），UI 走 `applySettings` 与 HDR 选项同一条路。
- 采集端按模式执行：`CaptureCardSource::setAudioIngress` + `configureAudio()` 里
  `位流优先` 先试压缩类型、失败再回退 PCM；`强制 PCM` 完全忽略位流类型并写日志说明。
  命令行测试开关 `--capture-audio 0|1|2`。
- **修掉一个真 bug**：`setAudioIngress` 原先在采集源 `configure()` 之后才调用，
  第一次连接不会生效；现在移到配置之前（日志顺序 `capture-audio-ingress` →
  `device bitstream types` → `selected media type` 即为证）。
- 本机参考采集卡三种模式实测（本身无位流类型）：mode0/1/2 均 exit 0、330–335 帧、60fps、
  选中 48kHz/2ch PCM（`logs/fsr/capture-audio-mode{0,1,2}.log`）。**位流优先的"真的优先"分支
  只能靠有 Dolby 输出的设备验收**，本机无法触发。
- 回归：契约测试 165 项 0 失败；预设 66 组迁移 + 全字段往返（含新模式）通过；
  delivery 短测 PASS（`logs/delivery/72b1dec41e0540f7ae5fa6856f8a18b7/result.json`）。

新增 `include/veyra/sink/BitstreamAudio.h` + `src/sink/BitstreamAudio.cpp`（FFmpeg libavcodec/libswresample）：
按 KSDATAFORMAT/WAVE subtype 分类 AC-3 / E-AC-3(含 DD+ Atmos 载体) / TrueHD-MLP / DTS / DTS-HD/DTS:X，
S/PDIF 的 IEC 61937 突发自动解框，解码为交错 float PCM 并给出真实声道数与采样率。

采集接线（`CaptureCardSource::configureAudio`）：PCM 媒体类型仍然优先；当**没有任何 PCM 能连接**时，
按 TrueHD > DD+ > DTS-HD > DTS > AC-3 的优先级选压缩类型直通，首帧解码后用它报的声道数/采样率
配置并启动 `CaptureAudioSession`（`audioSessionDeferred`），随后按原有 `push()` 契约进入既有 5.1 管线；
状态面板新增"位流解码为 N 声道 (kind)"一行。本机参考采集卡没有位流类型（`device bitstream types=0`），
因此该分支不会被触发，PCM 路径实测不变：`capture:0:0:0` 425 帧 60fps、exit 0（`logs/fsr/capture-after-g2.log`）。

**本地可验证的部分已实测**：`veyra_bitstream_audio_test` 用同一份 FFmpeg 编码 2 秒 5.1 测试信号
（每声道不同幅度、LFE 用 60Hz 低音），再经 `BitstreamDecoder` 分块解码：

- AC-3 448kbps：6 声道、95232 帧，逐声道 RMS `0.3531/0.2824/0.2118/0.1765/0.1412/0.0706`
  → 与编码幅度（0.5/0.4/0.3/0.25/0.2/0.1 的正弦 RMS）逐项吻合，**声道映射与幅度都正确**；
- 同一份 AC-3 再套 IEC 61937 突发头：结果逐位一致 → 解框正确；
- E-AC-3 640kbps：同样 6 声道与同样的幅度序列。

**未验证**：真实采集卡的位流协商与长时稳定性（本机设备不提供位流）；TrueHD/DTS-HD 只验证了解码器存在
（`avcodec_find_decoder` 命中）而没有真实素材，不得对外宣称已支持这两种格式的实机采集。
证据：`logs/fsr/bitstream-decode-test.log`；delivery 短测 PASS（`logs/delivery/622908bcc14b47a381340e8662c5b0a2/result.json`）。

移植 `ImDreamt/MFGAdaUnlock-RenoDx`（MIT，`third_party_local/community/`）到产品库
`include/veyra/ngx/AdaMfgUnlock.h` + `src/ngx/AdaMfgUnlock.cpp`：两处 `0x1b0`(Blackwell) 架构比较
改写为 `0x190`(Ada)、PTX 中点修正（注入 temporal 参数 + 104 处 `mul.ftz.f32 ...,0f3F000000`
替换为 `%f136/%f134` + fatbin 截断为非压缩强制 JIT）、8 个 `dlfg_kernel` 描述符槽位重定向；
全部只改进程内映射镜像，磁盘文件不动、不重签名；失败即回滚。

本机 RTX 5070（Blackwell）只做**结构与补丁机制**验证，不做行为验证：

- `veyra_dlssg_unlock_probe`（默认只读扫描）：本机 `runtime_local/nvidia/nvngx_dlssg.dll`
  310.7.0.0 / SHA256 `135EAF07…E36F`，TimeDateStamp `0x69FB633C`、SizeOfImage `0x00745000` 与审计身份一致；
  扫描得到 **gates=2、descriptors=8、ptx=99362、midpoints=104、joinLabelUnique=1**——与上游全部结构假设逐一吻合
  （`logs/fsr/dlssg-unlock-scan.log`）。
- `--apply-test`：应用后回读 gates=0、descriptors=0（槽位已指向重建 fatbin），随后
  `release()` 回滚回 gates=2/descriptors=8/ptx=99362/midpoints=104，`applied=1 readBack=1 restored=1`
  → 事务与回滚机制在真实运行库镜像上成立（`logs/fsr/dlssg-unlock-applytest.log`）。
- 引擎接入：`EnhanceGraph::initNgxFeatures()` 在 DLSSG 能力查询前按
  `AdaMfgUnlock::adapterIsAda(vendor, deviceId)`（0x10DE 且 deviceId 在 0x2680–0x28FF）决定是否解锁，
  50 系直接不进入；`VEYRA_DISABLE_ADA_MFG_UNLOCK=1` 可关闭；`shutdown()` 里在 NGX core 释放后回滚。
  解锁后运行时若仍只报 1 张生成帧，会明确告警"当前 DLSS-G 不是审计过的本地版本"。
- **50 系不受影响实测**：`--fg-multiplier 6` 仍为 `multiFrameMax=5`、875 生成/177 真实、exit 0，
  日志里**没有任何** `ada-mfg` 行（门控直接跳过）（`logs/fsr/blackwell-6x-after-ada-unlock.log`）。

**未验证（必须 40 系实机）**：解锁后 3X/4X 是否真的生成并带真实运动；Ada 上 Blackwell 硬件
flip metering 缺失是否导致画面冻结（上游在 Streamline 侧有软件回退，我们直接走 NGX，没有那一层）。
测试命令：`veyra.exe --fg-multiplier 4 --smoke-seconds 15 <视频>`，期望日志出现
`[ada-mfg] ... unlock applied=1 gates=2 descriptors=8 kernel=1` 与 `multiFrameMax=5`。

先前的"只做可行性"结论已升级为**已接入并实测**：

- 新增 `include/veyra/gfx/FsrSrBackend.h` + `src/gfx/FsrSrBackend.cpp`（FidelityFX 超分上下文 +
  逐帧 `ffxDispatchDescUpscale`），`EnhanceGraph` 新增 `initFsrSr()` 与 `runSr()` 的 FSR 分支，
  只启用 FSR 超分时不再要求 NGX 核心；`videoSrQuality = 5`（`kVideoSrSrFsr`）作为新档位，
  设置项"AMD FSR 超分 · 3.1.x（N卡可用）"，非 NVIDIA 归一化不再关掉该档，
  创建失败走既有 `FailedBackend::Sr` 降级并提示（不静默直通）。
- 运行库目录统一为 `runtime_local/amd/fidelityfx/`（loader + framegeneration + upscaler），
  FSR 补帧路径同步改名后**回归通过**（218 真实 / 214 生成，exit 0）。
- 实测（RTX 5070 / 提供方 3.1.5）：
  - 播放器 `--video-sr 5`：206 帧、205 次 dispatch、0 失败、exit 0（`logs/fsr/smoke-fsrsr.log`）；
  - 非 NVIDIA 形状：`--video-sr 5 --flow-amd`（AMD FFX 光流）226 次 dispatch、0 失败、exit 0
    （`logs/fsr/smoke-fsrsr-amdflow.log`）；
  - 质量探针 `--sr-mode 5`：19/19、`d3dDiagErrors=0`（`logs/fsr/q-fsrsr-diag.log`）；
  - **同帧对照**（index=45、同 NR、4K 输出）：平均绝对差 0.31/255、平均亮度 115.98 vs 116.03
    → 内容正确（`tools/image_check/compare_sr.ps1`）；
  - **相对画质不如直通**：梯度能量 0.500 vs 0.563（比值 0.888），如实记录，不宣称更清晰。
- 新工具/改动：`tools/quality_probe --sr-mode`（-1 直通 / 0 DLSS / 5 FSR，用于确定性 A/B）、
  `tools/image_check/compare_sr.ps1`（内容一致性 + 梯度能量）、`--flow-amd` / `--flow-gpudis` 测试开关。
- 已知限制：HDR 输出不支持（主动不创建）、flow 必须与源同尺寸、
  GPU-based validation 与 FidelityFX 超分 dispatch 不兼容（探针显式跳过并打印警告）。
- 回归：delivery 短测 PASS（`logs/delivery/1f0f1976eca045b9a22529e9b8f11fa4/result.json`）、
  `veyra_repair_contract_tests` 160 项 0 失败、`veyra_repair_preset_tests` 全通过。

新增 `tools/fsr_upscale_probe`（目标 `veyra_fsr_upscale_probe`）：枚举超分提供方 →
建上下文 → 上传 64 像素棋盘 + 水平渐变（1280×720）→ FSR 放大到 2560×1440 → 回读校验内容。
本机 RTX 5070 结果（`logs/fsr/upscale-probe.log`）：提供方只有 **3.1.5 与 2.3.4**（4.x ML 不出现）、
CreateContext OK、Dispatch OK、回读 mean=127.33 / min=0 / max=255 / distinctLevels=15 /
棋盘相位校验通过 → **N 卡能跑 FSR 3.1.x 超分，4.1 必须等 AMD 实机**。

接入点、需要的输入（`srcRgba_` + 源分辨率光流 + 常量深度）、以及三个不能跳过的验证点
（MV 符号必须像 FG 那样先核对、常量深度的质量代价、画质 A/B）写在
`docs/FSR_UPSCALING_PLAN_2026-09-16.md`。**产品代码本轮未改，不算完成功能。**

定位到上游 `ImDreamt/MFGAdaUnlock-RenoDx`（MIT，ReShade addon，README 明确写"仅内存修改"），
已克隆到 `third_party_local/community/MFGAdaUnlock-RenoDx`（gitignore）。其解锁由四件事组成：

1. `DLSSGInstanceManager::PopulateParameters` 中 NVAPI 架构 id 与 `0x1b0`(Blackwell) 的比较（两种编码）；
2. 同一常量的第二处比较，驱动"是否真的生成"的能力标志（只改 1 不改 2 会出黑帧）；
3. **PTX 中点修正**：把插值核心里编译期常量 `0.5`（104 处 `mul.ftz.f32 ..., 0f3F000000`）改成
   核函数自身的 temporal 参数，并把 fatbin 在 sm_89 PTX 项之后截断、以非压缩方式重发，
   逼驱动走 JIT（否则驱动用 sm_89 cubin，改写无效）；
4. 关闭 Blackwell 的硬件 flip metering（该 patch 针对 Streamline 插件，Veyra 直接走 NGX，不适用）。

本轮**没有实施**，原因不是"做不了"，而是不能在本机证明：本机只有 5070（Blackwell），PTX 改写在
Blackwell 上会改变一条已经正常工作的路径，无法区分"补丁生效"与"破坏原生 MFG"。上游也明确
指出单改门控会出现黑帧/重复帧。下一步做法（写入计划文档）：把 1+2+3 逐项做成带模块身份、
模式校验、回滚的进程内补丁，只在 Ada 上启用，并用真实 40 系机器验收；在那之前
`VEYRA_TEST_FG_FORCE_MULTIPLIER=1` 只能用来观察未打补丁时的失败现象。

用户要求更新长期未维护的文档。审计发现 README/README_EN 已指向1.3.0，但 `docs/BUILD.md` 仍有1.2.0构建、打包和PS5标题，`docs/LOCAL_INTEGRATION_STATUS_2026-09-15.md` 仍把1.2.0写成当前未发布版本。

已更新：构建依赖与便携打包命令改为1.3.0，补充dav1d/RemotePlay对应说明；本地整合状态新增当前1.3.0发布、main/tag和远端资产核验，并把dc44c48候选、未发布结论和待办明确标为历史快照。没有改动运行时代码、SDK、DLL、版本标签或发布资产。

验证：`git diff --check`通过（仅换行格式提示）；随后提交文档并推送`nrvideo/main`。本次不重新构建、不替换便携包，功能与硬件验收边界继续以`docs/RELEASE_NOTES_1.3.0.md`为准。

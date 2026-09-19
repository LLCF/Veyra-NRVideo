# Veyra

<p align="center"><img src="assets/veyra-logo.png" alt="Veyra" width="200"></p>

[English](README_EN.md) | 简体中文

<p align="center">
  <a href="https://github.com/Likely7/Veyra-NRVideo/blob/main/REAMDE%20MP4.mp4">
    <img src="assets/readme-demo.gif" alt="Veyra 演示视频" width="960">
  </a>
</p>


Windows 视频播放器与采集卡增强工具。支持视频、图片、采集卡实时预览和 PS5 局域网串流，可组合使用超分辨率、NR 画面增强与补帧。

[下载 1.4.1 免安装版](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.1) · [更新记录](docs/RELEASE_NOTES_1.4.1.md) · [反馈问题](https://github.com/Likely7/Veyra-NRVideo/issues)

## 1.4.1 更新

**重点修复 RTX 30/40 DLSS 补帧**：修复无法开启、错误回落到 2X，以及 RTX 3060 开启后卡死的问题，支持最高 6X。修复持续播放后显卡未满载却一直显示“补帧受限”的调度问题，以及 6X 的批次数组、计时和呈现资源同步错误。RTX 50 保持原生路径。受影响用户已反馈测试可用，兼容解锁仍为社区实验功能，不代表所有型号和驱动组合均已验证。

**播放器与采集**：可选择 MKV 内嵌主/副字幕和音轨；优化多字幕大文件打开与拖动；修复 NR/SR 等设置重启丢失、播放时进入屏保或休眠、先开 NR 再开 XeSS 的呈现一致性问题，以及音频输出故障引起采集图误重启和画面卡顿。

**导出**：修复 NVENC 接口版本兼容和错误提示，取消导出资格门禁与结束后的整片逐帧复检，保留可变帧率时间间隔和所选音轨；HDR 选择 H.264 时自动使用 HEVC Main10。完整变化、验证记录和边界见 [1.4.1 更新说明](docs/RELEASE_NOTES_1.4.1.md)。

## 1.4.0 更新

**新增色彩页**：一条全浮点、位于所有效果器之前的调色链（总开关关闭时零开销）。面板按 Lightroom 的观感与手感做：渐变色轨、可拖控制点的曲线（单调三次样条，两端黑/白场点可自由拖）、四个颜色分级色轮、混色器 8 色点条 + 黑白开关、`.cube` LUT（含输入空间检查）、命名色彩预设与 `.vpcolor` 导入导出、撤销/重做、按住看原图，以及每个分组的“眼睛”临时停用。全链路线性光处理 + 输出抖动，避免色带；HDR 在线性域调色并在 tone mapping 之前生效。

**修复**：IMAX/HEVC 的 MKV 打不开（新增 D3D11VA 硬解路径）、导出整槽缺帧失败（诚实补上一帧）、调色参数不实时/暂停时不刷新、载入 `.cube` 概率闪退、导出不应用调色、PS5 串流连不上、原生采集格式只剩 2 帧、采集卡窗口被第三方直播工具识别不到、杜比/DTS 采集无声、AVerMedia 5.1 直通等 40 余项，逐条见[更新说明](docs/RELEASE_NOTES_1.4.0.md)。

**移除**：AMD FSR 补帧的界面入口（切回 DLSS 容易卡住、效果一般）；引擎后端保留。

<p align="center"><img src="docs/images/1.4.0/mjpeg-latency.png" alt="MJPEG 采集链路优化前后（2 分钟真机复测）" width="900"></p>
<p align="center"><small>MJPEG 压缩采集链路：2 分钟真机复测，1080p60 processCpu 1.987 → 0.364–0.375 ms（−81%），4K18 6.218 → 0.912–0.936 ms（−85%）</small></p>

<p align="center"><img src="docs/images/1.4.0/native-latency.png" alt="YUY2 等原生格式采集链路优化前后" width="900"></p>
<p align="center"><small>原生（YUY2/NV12/RGB）采集链路：1080p60 P95 持平，4K18 P95 −7.5%、processCpu −8.7%，两侧零丢帧</small></p>

<p align="center"><img src="docs/images/1.4.0/colour-latency.png" alt="调色链路开启前后延迟对比" width="900"></p>
<p align="center"><small>调色链路开/关（采集卡 YUY2 1080p60，各 2 分钟）：每帧多 0.037 ms GPU，软件侧延迟无可测变化，均满 60 fps 零丢帧</small></p>



## 下载与运行

1. 在 [Releases](https://github.com/Likely7/Veyra-NRVideo/releases) 下载 **Veyra-1.4.1-win64-portable.zip**，不要下载 Source code。
2. 完整解压到一个可写文件夹，双击 **Veyra.exe**。无需安装 SDK、Python 或开发工具。
   首次启动 NR、超分和内部补帧均关闭，确认基础画面后按需开启；导入旧设置会恢复原来的开关。
3. 使用当前显卡驱动。要使用 NVIDIA NR、DLSS、RTX Video SR 和 NVENC，需兼容的 NVIDIA RTX 显卡；本版本主要在 RTX 5070 上验证。

系统要求：Windows 11 x64、DirectX 12。便携包含当前功能所需运行组件，显卡驱动和采集卡驱动由系统提供。8K 超分和原生高分辨率增强需要更多显存，不保证每个组合都能实时运行。

升级时先退出旧版，解压到新目录。需要保留设置时，复制旧目录下的 `runtime_local/*.v1` 与 `veyra.ini`；不要用旧目录整体覆盖新版运行组件。

## 使用教程
如果这个项目对你有所帮助，欢迎请作者喝杯咖啡！你的支持是持续维护的最大动力

<img width="276" height="374" alt="7baf2084b2310a7e685c5355bf9307d3" src="https://github.com/user-attachments/assets/1438c8f7-08b4-46fd-ad5e-6d83c46cdec2" />

BUG反馈与新功能

<img width="318" height="476" alt="屏幕截图 2026-09-19 095641" src="https://github.com/user-attachments/assets/a4479395-1456-48c1-98d4-e343a2102c3b" />


### 视频与图片

专业模式顶部点击 **截图**，保存处理后的完整画面到系统“图片”文件夹下的 `Veyra Screenshots`，SDR 保存为 PNG，HDR 保存为保留浮点高亮的 JPEG XR（`.jxr`），需支持 HDR 的看图软件。

点击底栏“打开”选择文件。底栏提供播放、进度、音量、字幕和全屏；切换到专业模式后，鼠标位于画面上时可用滚轮缩放。

### 采集卡

1. 连接设备，关闭其他软件对同一采集卡的占用。
2. 点击“采集”，选择设备、分辨率、帧率、像素格式及音频设备。
3. 在“音频监听”中按来源选择 `[DirectShow]` 或 `[WASAPI]`；默认是不监听音频。WASAPI 端点用 Windows 设备 ID 保存，只连接你明确选中的输入，不会自动切到麦克风或系统回录。
4. 先关闭增强确认基础画面，再按需打开 NR、超分和补帧。同一模式下可优先试 YUY2 / NV12。
5. 若主机游戏为30fps、采集卡输出60fps，在专业模式的内容节奏中选择60转30；真正60fps内容使用原始采集节奏。

### PS5 串流

1. PS5 和电脑连接同一局域网，优先网线。在 PS5 **设置 → 系统 → 远程游玩**中启用远程游玩。
2. 点击软件 **PS5** 并搜索主机。搜不到时，在 PS5 **设置 → 网络 → 连接状态 → 查看连接状态**找到 IPv4 地址，手动填写。
3. 填入 **PSN Account ID**，不是昵称或 Online ID。实验性“登录 PSN”可打开索尼网页；完成登录后复制最终跳转网址，再点击“从剪贴板提交登录结果”获取 ID。
4. 首次配对仍需 PS5 **远程游玩 → 关联设备**的八位配对码。成功后选已保存主机直接连接，不需每次重新配对。
5. 手柄连接 **电脑**，DualSense 优先 USB。兼容设备支持触摸板、陀螺仪、震动及自适应扳机，效果取决于设备、连接方式和游戏。“仅观看”不转发 PC 手柄，也不保证同账号下 PS5 直连手柄与串流会话共存。

输入最高可选 1080p；2K / 4K / 8K 是本地超分目标，不是主机原生串流分辨率。H.264 / H.265 可选，码率请求范围 5–100Mbps，不保证主机实际达到请求值。自动解码优先硬解、失败回退软解，也可手动指定。

主机和 PSN 凭据加密保存在 **%LOCALAPPDATA%/Veyra/remoteplay**，绑定当前 Windows 用户，升级不需搬运；不要分享这个目录。退出 PSN 不删除主机配对。首次免配对码注册、外网串流未实现。

**实验 HDR**：H.265 HDR 在 Windows HDR 开启时可保留 HDR 输出，也可同时开启 NR、超分和补帧。NR / RTX Video SR 处理映射副本，再与原 HDR 基底合成，属于 HDR 保留增强，不是原生 HDR NR 模型；SDR 显示器仍先映射为 SDR。真实 PS5 HDR 与 Sony 登录仍待进一步验收。

### 增强与补帧

光流可选 NVIDIA NVOF、AMD FidelityFX 和 **GPU DIS · FAST 实验**，默认 NVOF。GPU DIS 供效果对比，本机 1080p 合成测试明显慢于 NVOF，不保证提升性能。

专业模式中分别开启 NR、超分和补帧。超分下方选择算法及目标尺寸；补帧页选择 DLSS 或 XeSS 及可用倍率。建议从实时 NR、较低 RTX Video SR 档位和2X补帧开始，结合对比画面与实时状态调整。

处理跟不上时降低画质、倍率或目标尺寸。四个阶段卡显示光流、NR、超分、补帧的 GPU 耗时；曲线显示光流、NR、超分、残差合成、补帧的总增强处理耗时；展开后的第一项另列画面额外延迟估计。两者口径不同，都不是按键到屏幕的实测延迟。

音画同步自动跟随软件处理链路，采集卡音视频共同的输入延迟不会重复补偿。轻微耗时波动不会逐帧停放声音；持续欠速时实时预览跳过过期的补帧或源帧增强机会，保持媒体时间前进。导出仍完整处理，不采用预览跳帧策略。

鼠标在设置上停留约半秒即可查看说明。默认超分→NR→补帧；“低延迟模式”改为 NR→超分→补帧，默认关闭，仅影响预览。可能降低耗时，也可能增加拖影或边缘瑕疵。

### Smooth Motion（NVIDIA App AI 插帧）

只用 Smooth Motion 时，在软件中将补帧倍率设为**关闭补帧**，再到 **NVIDIA App → 图形 → 当前使用的 Veyra.exe → AI 插帧 → 开**，应用后重启播放器。NR、超分可以照常使用；专业模式的补帧页也提供展开说明。

只想用 DLSS / XeSS 时，在 NVIDIA App 关掉 AI 插帧并重启，再选择软件补帧。也允许两者同时开启，不作拦截；叠加效果未验证，可能增加重影、延迟或 GPU 负担。软件的“关闭补帧”和总增强开关不会关闭驱动功能。

软件 FPS、耗时与队列不包含驱动生成部分；截图、导出不包含驱动中间帧。额外音画延迟、直播捕获结果需自行验证。本机用户已反馈 Smooth Motion 有效且稳定，不代表所有显卡和驱动均已验收。

### 导出与运行组件

在专业模式选择图片保存或视频导出、格式与输出位置。视频导出会在结尾核对完整性，可能需要额外时间，可取消。内嵌字幕暂不保留；不要让多个实例同时导出到同一个目标文件。XeSS 目前仅用于预览；视频导出使用已支持的 DLSS 路径。

允许自行替换 DLL：退出软件后，NVIDIA 文件放在 `runtime/experimental/`，XeSS / XeLL 放在 `runtime_local/intel/experimental/`，保留文件名。软件不锁定哈希或签名；清单仅记录发布包原件，替换版的接口与硬件兼容性不作保证。卸载整个软件只需退出后删除解压目录。

### NR 运行版本

专业模式 → 增强 → NR 运行版本，可选 NVIDIA 原版、RTX 40/50 社区版或 **RTX 30 兼容 · 实验**。选择版本后再手动开启 NR。切换会短暂停顿，失败恢复上一套设置。

两种社区组件分别在 `runtime/experimental/nr-community/` 和 `nr-ampere/`，无需覆盖原版；均为修改文件，签名状态 `HashMismatch`。三种运行版本已在 RTX5070 验证切换和 NR 执行。1.4.1 的 RTX30/40 修复经受影响用户反馈可用，具体性能与画质仍取决于显卡、素材和设置。NR 运行版本与 DLSS 补帧分别设置；RTX30/40 最高 6X 由补帧兼容层提供。

### OBS 直播与录制

添加“窗口采集”，选择 Veyra，并将“捕获方式”手动设为 **Windows 10（1903及以上）**。不要依赖“自动”：它可能选中 BitBlt，导致只捕获 UI、视频区域没有画面。本机已通过切换到该 Windows 捕获方式恢复视频。

软件里的“直播兼容 · 实验”只切换显示交换链，不能解决 BitBlt 捕获问题，默认关闭即可。其他录屏软件优先选 Windows Graphics Capture / WGC；未验证所有录屏软件及补帧输出的捕获节奏。

## 技术路线与边界

<img width="332" height="576" alt="123" src="https://github.com/user-attachments/assets/3cd48fca-2f7b-418a-8b0e-0848f6bde556" />


C++20、Win32、D3D12；文件由 FFmpeg 处理，采集使用 DirectShow，视频导出使用 NVENC。各入口共享增强管线，采集保留最新帧，光流提供估算的运动信息。

NR 与 DLSS 帧生成属于 **community experimental / 社区实验集成**，不是 NVIDIA 官方认证或完整游戏原生集成。采集画面没有游戏引擎的原生深度和运动数据，效果可能存在拖影或细节变化。AMD NR 暂未提供；FRUC 已移除；HDR 适用范围见下文；AV1 / ProRes 导出尚不支持。实卡兼容性与长期稳定性仍需持续验证。

## 开发与许可

[构建说明](docs/BUILD.md) · [组件清单](docs/RUNTIME_COMPONENTS_1.4.1.md) · [第三方许可](THIRD_PARTY_NOTICES.md)

Veyra 原有源码采用 [GPLv3](LICENSE)；含串流的组合程序同时适用 [AGPLv3 与上游 OpenSSL 例外](licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt)。应用源码对应版本标签，Release 另附串流依赖与 FFmpeg 对应源码包，普通用户无需下载。SDK、模型和运行时不进入源码仓库；Release 组件按各自许可与实验发布范围单独提供。

## HDR 与 5.1（1.2.0 起，1.3.0 保留）

以下能力在 1.2.0 引入并由 1.3.0 保留。真实 HDR 屏、5.1 扬声器和不同采集卡仍须逐台验收。

- **HDR 输入 / 增强**：文件、P010/P016 采集与 PS5 HDR；支持明确标记的 BT.2020 NCL / PQ 或 HLG。Windows HDR 开启时，可组合 NR、DLSS SR / RTX Video SR、DLSS / XeSS 补帧。NR / Video SR 使用 SDR 代理和 HDR 基底合成，压缩高光和近黑区域的增强会衰减；不把 SDR 结果逆造为原始 HDR。HLG 使用 1000nit / gamma 1.2 参考转换。
- **SDR 显示开关**：采集卡面板的“转为 SDR 显示”默认关闭，控制所有实时预览。打开后将 HDR 映射为 SDR，增强照常可用；播放中切换无需重连，可能短暂停顿。关闭后跟随显示器 HDR 状态。截图跟随当前画面，视频导出保持原有 HDR 规则。
- **采集颜色**：默认使用设备元数据。设备漏报时，在采集面板选择手动 PQ 或 HLG，且必须选择 P010/P016。10bit 本身不代表 HDR；RGB / YUY2 HDR 及 BT.2020 constant-luminance 暂不支持。
- **HDR 导出 / 截图**：视频选择 HEVC，输出 Main10 / BT.2020 / PQ；HLG 也统一输出 PQ。支持 NR、超分和内部 DLSS 补帧。XeSS 仍只支持预览；H.264 不承载此 HDR 导出。截图为 FP16 scRGB JPEG XR。导出不虚构或沿用处理前的峰值元数据；普通 PNG/JPEG 保持 SDR。
- **5.1 音频**：文件解码与采集 PCM 保留声道位置，共用一套音频时钟、补偿和音量处理。采集优先尝试设备真实提供的多声道模式。Windows 输出设备须配置成 5.1；仅支持立体声时明确降混，在详细状态中显示输入 / 输出声道数。PS5 保持上游提供的立体声，不伪装成 5.1；本轮不提供 Dolby/DTS 压缩码流直通或 Atmos 对象音频。

本机 RTX 5070 已验证 GPU 输出、HDR 文件导出及多声道软件数据；真实 HDR 屏观感、5.1 扬声器定位和各采集卡仍需实机验收。各组合记录及边界见 [施工记录](docs/HDR_MULTICHANNEL_EXECUTION_2026-09-14.md)。新安装默认仍关闭全部效果。

## 致谢

感谢 [Magpie Experimental](https://github.com/SAOG0721/Magpie/tree/experimental) 在 NR 残差合成、光流与增强处理链方面提供的研究启发；感谢 [chiaki-ng](https://github.com/streetpea/chiaki-ng) 提供 PS5 串流基础，以及 [XeSS-GPU-Motion](https://github.com/gggz114514-oss/XeSS-GPU-Motion) 的 GPU DIS 光流实现。

其他依赖、来源与许可证见 [第三方说明](THIRD_PARTY_NOTICES.md)。

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码）；有问题或想第一时间拿到 beta 版，欢迎进群反馈。

<p align="center">
  <img src="docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="docs/images/1.4.0/community-group.jpg" alt="Veyra 交流群 / bug 反馈 / beta 测试" width="220">
</p>
<p align="center"><small>左：微信赞助（自愿，不影响任何功能的可用性）　右：Veyra 交流群——bug 反馈与 beta 版本发布</small></p>

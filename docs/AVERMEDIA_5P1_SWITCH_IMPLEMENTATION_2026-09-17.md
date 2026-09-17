# AVerMedia 5.1 采集开关：实现与验证

日期：2026-09-17
分支：`codex/avermedia-51-switch-20260917`（独立 worktree `out/worktrees/avermedia-51`，基于 `6119a7d`）
上游调研：[`AVERMEDIA_5P1_CAPTURE_FEASIBILITY_2026-09-17.md`](AVERMEDIA_5P1_CAPTURE_FEASIBILITY_2026-09-17.md)

目标：让 Veyra 也能从 GC553G2 / GC553PRO / GC575 拿到 5.1——也就是把 AVerMedia 的 OBS 插件
真正做的那一步自己做掉，而不是再写一份可行性分析。

---

## 1. 做了什么

### 1.1 新模块：`include/veyra/source/AverMediaAudioSwitch.h` + `src/source/AverMediaAudioSwitch.cpp`

按绝对路径加载用户**已经装好**的厂商组件，调用与 OBS 插件完全相同的入口点：

```
Qt6Core.dll (OBS bin\64bit)  ->  预加载，避免依赖靠 PATH 猜
avt_device_opener.dll        ->  LoadLibraryExW + 13 个修饰名导出
  AVerMedia::VendorSdk(dataDir) -> initialize()
  AVerMedia::DeviceOpener       -> SetVendorSdk() -> SwitchDeviceThenDetectAudioFormat({name,path})
                                -> IsAudioFormatNonPcm() -> StartChecking()
  VendorSdk::getAudioFormat()   -> 唯一可信的成功判据（原因见 1.3）
```

- **不分发任何第三方二进制**：组件只从用户自己的安装目录加载；找不到就静默降级，基础播放/采集/导出不受影响。
- 发现顺序：环境变量 `VEYRA_AVERMEDIA_COMPONENT`（诊断覆盖）→ `HKLM\SOFTWARE\AVerMedia\OBS_Plugins\AVerMediaMultichannelAudio\Path` → `%ProgramFiles%\obs-studio`。
  三种来源都要求同一个目录布局（`obs-plugins\64bit\avt_device_opener.dll` + `data\obs-plugins\AVerMediaMultichannelAudio\RTICE_SDK_x64.dll` + `RTK_IO_x64.dll`）。
- 外来代码可能抛 C++ 异常也可能触发 SEH：调用被拆成"只含 POD 的 `__try/__except` 调用器 + 外层 `catch(...)`"，任何失败都变成一条日志，不允许把采集进程带走。
- `stop()` 幂等；DLL 故意**不** `FreeLibrary`（其监控线程与 Qt 运行时在进程末尾卸载不安全）。

### 1.2 接进采集源：`src/source/CaptureCardSource.cpp`

- 音频图**建立之前**先武装开关（媒体类型枚举必须看到切换后的状态）：`configure()` → `applyVendorAudioSwitch(name,path)` → 再 `connectDirectShowAudio()`。
  只对 DirectShow 独立音频设备、且设备路径含 `vid_07ca` 时才尝试；WASAPI 端点与"视频设备内置音频"不动。
- `binding=separate ...` 日志新增 `device="<友好名>"`：这是上一次复盘里最缺的信息（当时无法确认 index 0/2 到底是哪个端点）。
- 嵌入式音频引脚不存在（`VFW_E_NOT_FOUND`）被判为**静态不可用**：不再每 5 秒重试（上次现场日志里空转了 7 次、20 秒、21 行噪音）。
- `close()` 关闭开关。

### 1.3 为什么用 `getAudioFormat` 当成功判据

`SwitchDeviceThenDetectAudioFormat` 返回 `void` 且内部吞掉错误，只有它自己会打印失败信息。
而 `vendor initialize/setDevice/setPort/AT_Audio_Send_Non_Pcm_Data` 的返回值都在内部被丢弃，
唯一能确认"真的跟卡说上话"的接口是 `AT_Audio_Get_HdmiRX_AudioFormat`：

```cpp
if (!formatOk) { state=Failed; detail="card not reached: getAudioFormat ret=..."; return false; }
```

芯片返回 **20 (0x14)** 表示 HDMI 源正在发非 PCM（Dolby）；其他值表示 PCM。
注意这是**源的当前格式**，不是"有没有武装成功"——用户在菜单里时它一样是 PCM。

---

## 2. 实测证据（全部来自本机，可复核）

### 2.1 构建

```
out\worktrees\avermedia-51\out\build-avermedia-51.cmd        exit 0
（CMake presets x64-release + Ninja，DLSS SDK / FFmpeg / clip-tools 指向 C:\veyra-deps 与主检出的 third_party_local；
  RemotePlay=OFF 以缩短隔离构建时间——与发布构建的这一处差异必须单独复核）
日志：out\worktrees\avermedia-51\out\build-7.log（213 个目标）
```

### 2.2 导出名表与真实二进制的比对

```
python + pefile 对比 avt_device_opener.dll(1.0.2, SHA256 100A3D2E…82F1) 的导出表：
required exports present: 13 / 13
```

即 1.1 里那张修饰名表没有一个是猜的。

### 2.3 单元测试（无硬件、无 OBS）

`tests/unit/AverMediaAudioSwitchTests.cpp` → `veyra_avermedia_switch_tests.exe`

裸机（本机没有装 OBS 插件）：
```
ok   vid_07ca path is recognised
ok   uppercase VID_07CA is recognised (case-insensitive)
ok   a non-AVerMedia device is rejected
ok   an empty path is rejected
ok   a WASAPI endpoint id is rejected
note component not installed on this machine (expected on a bare test host)
ok   locateComponentRoot returns empty when absent
ok   apply() reports failure when the component is absent
ok   status marks the component as absent / DLLs not loaded / 有原因可记
ok   status state is Unavailable
ok   stop() leaves the helper inactive
ok   destructor after stop() is safe
PASS (0 failures)
```

### 2.4 用**真实组件**跑通加载链（本次最有价值的一条）

把安装包解出的真实文件铺成同一套目录结构（`%TEMP%\averma-fake-component`），并放入一台现成的
`Qt6Core.dll`（6.8.3.0，来自本机其它软件，仅用于本地验证，不进任何交付物），然后
`set VEYRA_AVERMEDIA_COMPONENT=...` 再跑同一个测试：

```
DeviceOpener::SwitchDeviceThenDetectAudioFormat, closePort ret: 0
SetupDiGetDeviceInterfaceDetail : 1 / Not found the device   × N   （本机没有插卡）
serialNumber:
uvcName: USB3 Digital Audio                                        （我们传入的结构化参数被正确读走）
Fail to open COM1                                                  （厂商自己的兜底路径）
ok   located component provides avt_device_opener.dll
note apply() with component present returned 0: card not reached: getAudioFormat ret=5
PASS (0 failures)
```

这条同时证明了四件事：

1. Qt 预加载 + `LoadLibraryExW` 路径策略有效（同一个 DLL 在只加 Qt 之前会以 126 失败，见下）；
2. 13 个导出在**运行时**全部解析成功；
3. 手写的 `DeviceOpenerParam{std::wstring name; std::wstring path;}` ABI 镜像被真实组件正确读取（它打印出了我们传的 `uvcName`）；
4. 没有卡时是**干净失败**：进程不崩、不挂，`getAudioFormat ret=5` 就是"组件在、卡不在"的签名，而且我们的返回值如实报 0（false）。

对照实验（未加 Qt 时）：
```
LoadLibraryExW(avt_device_opener.dll) -> 失败，err=126 (ERROR_MOD_NOT_FOUND)
LoadLibraryExW(RTICE_SDK_x64.dll)     -> 成功（RTK_IO_x64.dll 在同目录，靠 DLL_LOAD_DIR 解析）
```

### 2.5 项目交付短测

```
powershell -File scripts/gates/delivery.ps1 -Root <worktree> -BuildDirectory <worktree>\out\build
DELIVERY SHORT GATE PASS   (exit 0)
logs\delivery\084be258e4834f7d958efa997a0b40c1\result.json
```

---

## 3. 用户侧怎么验收（这是唯一还没走完的一步）

1. 机器上装好 AVerMedia Multichannel Audio 组件（他已经在 OBS 里用上了，说明组件与固件都满足）。
2. PS5 输出设 **Dolby Digital**，进游戏（菜单一般不出 5.1）。
3. 用 Veyra 连接采集卡的**独立音频设备**（不要选"视频设备内置音频"，那张卡没有内置音频引脚），
   采集音频模式选「自动」或「位流优先」。
4. 看日志里这行 `[capture-audio-vendor]`：
   - `non-PCM switch armed ... chipFormat=20 nonPcmNow=1` → 切换生效，源正在发 Dolby；
   - `chipFormat=<其它>` → 组件在跑但源不是 Dolby；
   - `card not reached: getAudioFormat ret=5` → 没插卡/被别的软件占着；
   - `not installed` → 组件没找到（可用环境变量指路）。
5. 紧接着看 `[capture-audio-bitstream] device bitstream types=?`：
   - **非 0** → 卡切换后真的换了媒体类型，现有"位流优先"链路直接吃下，5.1 应当立刻可用；
   - **仍然是 0** → 说明切换只改载荷不改媒体类型（调研报告 2.4 的推断成立），下一步要做的是
     在 PCM 标签流上嗅探 IEC 61937 同步字 `F8 72 4E 1F` 并按 data-type 选解码器；**在那之前，
     这种情况下的声音会是位流被当 PCM 播出来的噪音，不是正常的 2.0**。

第 5 步是这次唯一无法在本机回答的问题——本机没有卡，也没有 5.1 源。

---

## 4. 未验证 / 已知限制（不得含糊）

- **真卡未验收**：本机没有 GC553G2，`SwitchDeviceThenDetectAudioFormat` 走到"找不到设备"就结束了；
  真正把卡切到 non-PCM、以及切换后端点媒体类型是否变化，都还没有观测。
- **只验证到组件加载与调用**：`initialize` 后的真实设备交互、`StartChecking` 线程在长时运行下的行为未验证。
- **退出不还原模式**：`stop()` 只 `StopChecking` + `closePort/uninitialize`，不调 `setNonPcmOnOff(0)`
  ——这与厂商插件行为一致（它也不还原）。副作用是：之后若用**没有**装插件的软件采集同一端点，
  可能听到位流当 PCM 的噪音。要不要在关闭时还原，等真机结果再定。
- **没有 UI 开关**：目前是"检测到 AVerMedia 设备 + 组件存在就自动武装"。若将来需要按需关闭，加一个设置项即可。
- **厂商自己的日志走 stdout**：`avt_device_opener.dll` 在没有安装 log handler 时直接打印
  （`Not found the device`、`Fail to open COM1` 等）。没有接 `SetLogHandler`，因为它的参数是
  `std::function`，跨 DLL 边界构造的 ABI 风险高于收益。这些行会出现在控制台，不进 Veyra 日志文件。
- **隔离构建 RemotePlay=OFF**：本分支的验证构建关闭了 RemotePlay；提交前需要一次与发布一致的完整构建。
- **不是所有 AVerMedia 卡都适用**：本模块按 `vid_07ca` 放行，真正支不支持 5.1 由卡与固件决定；
  官方只列 GC553G2 / GC553PRO / GC575（GC553G2 需固件 ≥ V1.0.7.7），且源必须是 Dolby 位流，不支持 5.1 LPCM。

---

## 5. 改动文件

| 文件 | 说明 |
|---|---|
| `include/veyra/source/AverMediaAudioSwitch.h` | 新模块接口 |
| `src/source/AverMediaAudioSwitch.cpp` | 发现 / 加载 / 调用 / 降级 / 停机 |
| `src/source/CaptureCardSource.cpp` | 建图前武装开关；记录音频设备名；嵌入式引脚静态失败不再重试 |
| `include/veyra/source/CaptureCardSource.h` | 新增 `applyVendorAudioSwitch` 声明 |
| `tests/unit/AverMediaAudioSwitchTests.cpp` | 无硬件单元测试（14 项） |
| `CMakeLists.txt` | 新源文件 + 测试目标 |

未提交任何 SDK / DLL / 模型 / 用户配置；`third_party_local`、`runtime_local`、`loop\local` 在隔离 worktree 里
是**目录联接**（只读复用主检出的本地依赖），不参与版本控制。

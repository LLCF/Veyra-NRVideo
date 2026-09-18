# Veyra 1.4.2beta 内测版

基于 1.4.1 的隔离内测构建，尚未发布正式版。

## 本次变化

- 新增 RTX Video HDR：将 SDR 视频转换为 HDR，接入共享增强链，可与现有 NR、SR、补帧组合。
- 增强设置中勾选「RTX Video HDR」，可调整对比度、饱和度、中间灰和峰值亮度。
- 支持转换后的 HEVC Main10 HDR 导出；原生 HDR 输入不重复转换。
- 开关下显示实际预览状态。显示器和 Windows 必须启用 HDR 才会进行 HDR 预览；SDR 显示器保持 SDR，不会故意显示灰蒙蒙的画面。HDR 导出不要求本机显示器支持 HDR。
- 撤回画质不达标的 NVIDIA FSR 4.1 实验，保留 DLSS SR、RTX 视频超分和原有官方 FSR。旧实验预设自动转为 RTX 视频超分「高」，其余设置保留。
- 保留 1.4.1 的 RTX30/40 DLSS 兼容、最高 6X、持续播放补帧受限、MKV 字幕/音轨切换、设置保存等修复。

## 内测重点

请在 HDR 显示器上开启 Windows HDR，再比较同一 SDR 片段的开关效果；同时反馈显卡、驱动、显示器型号和日志。重点观察高光、肤色、字幕、调节即时生效、切换视频以及与 NR/SR/补帧组合时的稳定性。

现有证据来自 RTX5070 / 616.56；TrueHDR 创建、执行、组合增强和 Main10 导出在前序隔离测试通过。实际 HDR 屏幕显示、RTX30/40 HDR、采集卡和 PS5 本轮仍需内测，不宣称全设备验收。实验运行组件不代表 NVIDIA 官方认证或支持。

完整解压后运行 Veyra.exe。包内不含个人配置；原配置可自行保留。对应源码见同目录 Veyra-1.4.2beta-source.zip。

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码，完全自愿，不影响任何功能）；有问题或想第一时间拿到 beta 版，欢迎进群。

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/community-group.jpg" alt="Veyra 交流群 / bug 反馈 / beta 版本" width="220">
</p>
<p align="center"><small>左：微信赞助　右：Veyra 交流群（bug 反馈与 beta 版本发布）</small></p>

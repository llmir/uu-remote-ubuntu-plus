[English](../CHANGELOG.md) · [العربية](CHANGELOG.ar.md) · [Deutsch](CHANGELOG.de.md) · [Español](CHANGELOG.es.md) · [Français](CHANGELOG.fr.md) · [日本語](CHANGELOG.ja.md) · [한국어](CHANGELOG.ko.md) · [Русский](CHANGELOG.ru.md) · [Tiếng Việt](CHANGELOG.vi.md) · [简体中文](CHANGELOG.zh-Hans.md) · [繁體中文](CHANGELOG.zh-Hant.md)

[首页](README.zh-Hans.md)

# 更新记录

桥接发布标签与批准的 Windows UU 版本分别管理。

## 未发布

### 修复

- 控制端短暂发送越界终端尺寸（例如先 120×9001，后 120×30）时保留会话与原 PTY 尺寸，等待合法更新；格式错误的帧和实际 PTY 调整失败仍关闭对应会话。

## Plus 0.2.0-work — 2026-10-06

开发源码更新；Plus 0.1.0 仍是最近的标签版本。

- 可选 Ubuntu 终端会话保留，断线重连回到原工作区；改善原始终端 I/O 与会话交接，安装默认仍为新会话。
- 多文件剪贴板入站接收，显示实际字节进度，支持取消和中断批次清理；仅支持普通文件。
- 原始 PNG 与 DIBV5／DIB 一起保留，并统一原生剪贴板处理入口。
- 可选 Mac PNG → TIFF 助手，提供本机设置窗口。
- 可选原生捕获实验：MIT CPU 适配与独立 AGPL-3.0 GPU 组件，尚无真实会话性能提升结论。
- 11 种语言首页与更新记录同步，补充中英文配图和使用入口。

已知限制：快速批量终端输入、当前 Ubuntu → Mac 图片验收与双控制端焦点仍待解决；出站文件与目录复制未实现。本次不包含近期焦点／公共输入试验候选。
[更新说明](../docs/i18n/zh-Hans/updates/2026-10-06.md) · [工作版本详情](../docs/releases/plus-v0.2.0-work.md)

## Plus 0.1.0 — 2026-10-03

Plus 基于 MIT 许可的上游桥接。

### 新增

- 桌面尺寸：最高 4K 的四档选择、完整桌面适配与保存尺寸恢复。
- 手机文字：Plus 输入路径迭代与公开 FreeRDP 文本路径。
- 管理窗口：管理窗口／弹窗独立捕获，复用查看器会话。
- 光标：主题光标回退与启动修正，默认保持关闭。
- 桌面动作：接入 GNOME 桌面／概览动作。
- 桌面工具：集成画质／VNC／FreeRDP／Openbox 入口，局部字体、DPI 与凭据输入。
- 构建与恢复：固定补丁源码构建、运行时检查和档位／查看器恢复。
- 十一种语言的首页和核心指南，以及可编辑的架构、迁移和对比图。

### 修复

- 剪贴板：SDL 源码补丁处理后台刷新、剪贴板归属变化与格式缓存。
- 管理窗口重开、UTF-8 标题、所属弹窗捕获与关闭查看器后的焦点返回。
- 手机文字提交、输入钩子初始化、部分输入处理与原生终端限时退出。
- 完整画布鼠标映射、独立尺寸／码率设置、手动 RDP 会话保护和可恢复工具启动器。

### 兼容

- Ubuntu 24.04 / GNOME 46 · Ubuntu 26.04 / GNOME 50 · x86-64。
- Windows UU：默认采用已审核的 4.42.0.2770。

## 继承上游 — 未发布

上游提供 Unicode 语义剪贴板事务、输入法编辑与长语音批次、实体键盘布局、认证主机输入、网络／运行时诊断、Wine 蓝牙驱动隔离和无人值守会话恢复。

## 上游 0.2.0 — 2026-07-18

- 网络诊断和已安装源码摘要；可选默认／固定网络适配器，不替换主机路由。
- GNOME RDP 描述符恢复、libei 回移、描述符限额、会话 Keyring 就绪及系统 Python／GI。
- 可配置手机文本节奏、零延迟物理键；优先原始 `SendInput`，代理回退，确认中继焦点。
- 认证 X11／XTEST 输入及分类遥测；替换 Xvfb／中继会话清理、网络恢复去抖。
- XRDP 键盘布局／重连与无人值守说明。

[中文发布说明](../docs/i18n/zh-Hans/releases/v0.2.0.md) · [原版 0.2.0 发布](https://github.com/lachlanchen/uu-remote-ubuntu-bridge/releases/tag/v0.2.0)。

## 上游 0.1.0 — 2026-07-17

- 首个支持版本：Wine／UU、Xvfb、SDL FreeRDP、GNOME 中继、用户输入代理、重新注入和监督服务。
- 保存尺寸／端口／显示配置、审核二进制修改、回退、普通 RDP 剪贴板。
- 可选 TPM2／GDM 无人值守启动。
- 手机 Unicode／虚拟按键规范化、Wayland／Xorg／XRDP 会话 D-Bus 发现、Wine 事件日志兼容和旧前缀清理。

[中文发布说明](../docs/i18n/zh-Hans/releases/v0.1.0.md) · [原版 0.1.0 发布](https://github.com/lachlanchen/uu-remote-ubuntu-bridge/releases/tag/v0.1.0)。[完整固定上游历史](https://github.com/lachlanchen/uu-remote-ubuntu-bridge/blob/e2854e2b/CHANGELOG.md)保留原始详细变化及验证记录。

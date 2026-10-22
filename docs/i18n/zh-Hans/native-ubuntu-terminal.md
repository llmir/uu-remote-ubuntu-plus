[English](../../native-ubuntu-terminal.md) · [中文（简体）](../zh-Hans/native-ubuntu-terminal.md)

[← 返回简体中文首页](../../../i18n/README.zh-Hans.md)

# 通过 UU 使用原生 Ubuntu 终端

UU 控制端的 **Terminal** 可打开主机真正的 Ubuntu 登录 shell。界面仍可能叫 **PowerShell**；对 Wine 中的 Ubuntu 主机，这个名字是兼容入口，最终并不运行 PowerShell。

选择 PowerShell 后，PTY 使用与 `uu-remote-bridge.service` 相同的普通用户，起始目录为 home，加载登录 shell 配置，支持交互程序、UTF-8 和控制端尺寸变化。

终端尺寸更新接受 1–1000 列和 1–1000 行。格式合法但尺寸越界的更新会被忽略，保留当前 PTY 尺寸与会话，之后的合法更新正常生效；例如控制端启动时先发送 120×9001，再发送 120×30。格式错误的帧或实际 `TIOCSWINSZ` 失败仍会关闭对应终端会话。

## 控制端兼容性

终端能否打开取决于控制端平台、UU 应用版本和主机终端协议。若在 shell 打开前出现 `Client version too low` 或串流错误，记录两端版本，检查官方控制端的 Terminal 入口，再尝试新建终端会话，以实际 Ubuntu shell 输出确认连接。

会话列表查询也可能初始化 UU 传输。远程命令与文件传输工具应保留退出状态和原始数据；交互命令使用原生终端，文件使用已验证的 SSH/scp 路径。

## 终端立即显示 exit code 0

Wine 的 `powershell.exe` 可能是兼容占位程序，没有打开 shell 就成功退出。安装的 `uu-terminal-proxy.exe` 接管 UU 的 PowerShell 入口，把终端流送入 Ubuntu 原生 PTY。若终端立即关闭，重新安装当前桥接器并检查终端代理与已安装的适配器。`cmd` 入口用于 Wine 诊断命令处理器。

```text
UU 控制端 Terminal
  → 已认证 UU 终端通道
  → Wine GameViewerServer / conpty_bridge.exe
  → bin/powershell.exe（uu-terminal-proxy.exe）
  → 随机 token + framed I/O，127.0.0.1
  → uu-terminal-bridge → forkpty() → Ubuntu 登录 shell
```

## 为什么不使用 SSH

UU 已认证控制端并送达终端流，再 SSH 登录本机会增加常驻 listener、认证路径和密钥。代理只转发 stdin/stdout/尺寸，原生 broker 创建 PTY。常规 SSH/scp/rsync 属于[SSH 与端口映射](ssh-and-port-mapping.md)，不替换此通道。

每次启动生成 256-bit token。UU 的 `CreateProcessAsUser` 重建环境，代理不能依赖 service-only 变量，因此 launcher 将 port/token 写入代理旁 `uu-terminal-bridge.runtime`，模式 `0600`，停止时删除。ready 文件只有 port，参数无 token。旧文件无法连接已停止的 broker，下次原子替换。listener 仅 IPv4 回环，最多四会话，日志只有 metadata，不保存命令、输出或文字，不增加 root 权限或绕过 sudo。

## 提示符显示

窄终端中的 `$` 可能换行，因为 UU 对标题、颜色和 Readline 控制的宽度计算不同。桥接器保留 `TERM=xterm-256color` 与正常登录 shell，保持正确的光标位置和交互输入。输入文字位置异常时，关闭终端并重新建立会话；本地终端继续使用原来的 shell 配置。

## 安装和使用

`uu-shell` 按 peer profile 明确选择 `terminal` 或 `ssh`，不会失败后偷偷换路：

```bash
uu-ssh add lab --port 22709 --user YOUR_REMOTE_USER \
  --device-id UU_DEVICE_ID --shell-transport terminal
uu-shell lab
uu-shell lab --session-id SESSION_ID
uu-ssh add lab --port 22709 --user YOUR_REMOTE_USER --shell-transport ssh
uu-shell lab 'hostname; id -un'
uu-shell --help
```

terminal 默认新建会话，session-id 显式恢复；SSH 执行已固定的 OpenSSH alias。两者按字面传参数、保留 I/O、signal 和 exit status，不启动 daemon、桌面或 Port Mapping，不重试/回退。文件传输另测，用已验证 SSH/scp，不能把粘贴终端文本当文件通道。

只更新入口而不重启桌面：

```bash
install -m 0755 scripts/uu-ssh scripts/uu-shell "$HOME/.local/bin/"
```

正常安装构建两个 helper：

```bash
./install.sh --skip-packages --skip-account-login
```

`GameViewer/bin/powershell.exe` 只有在不存在或已是本仓库代理时才替换，未知文件拒绝。原生 broker 退出触发整体 UU 服务恢复。控制端打开设备→Terminal→保留 PowerShell→运行 Ubuntu 命令，`exit` 关闭；`cmd` 仍是 Wine 诊断命令处理器。

## 断线后保留终端（可选）

默认 `UURB_TERMINAL_SESSION_MODE=fresh` 每次打开新登录 shell，断开时结束它。安装包含此功能的版本后，可在现有服务环境文件中保存：

```bash
sed -i '/^UURB_TERMINAL_SESSION_MODE=/d' ~/.config/uu-remote-bridge/environment
printf '%s\n' 'UURB_TERMINAL_SESSION_MODE=persistent' >> ~/.config/uu-remote-bridge/environment
```

设置在下一次计划内桥接服务重启时生效；`uu-remote restart` 也会断开桌面连接。仅保存设置不会重启，安装会保留已有值，也不会默认开启持久会话。恢复新会话模式时把上面的 `persistent` 改成 `fresh`，再在计划内重启时应用。

持久模式使用 `/usr/bin/tmux`、独立 socket 和 `main` 工作区，不加载个人 tmux 配置，不复用日常 tmux 服务。所有 UU 终端共享这个工作区、输入和 shell 状态。关闭主控终端只分离客户端；重开回到原 shell、目录与任务。`exit` 或 Ctrl-D 仍结束 shell。桥接服务重启、注销和重启系统不在保留保证内。

## 当前验收 · 2026-10-06

当前源码的真实 Mac UU 关闭／重开保留了同一 shell、目录与后台任务；普通输入、Ctrl-C、尺寸变化和清屏通过。快速批量输入仍有未解决的边界，普通输入通过不能扩大为该情况也通过。参考环境为 Ubuntu 26.04／GNOME 50、Wine 11 和 Windows UU 4.42.0.2770；其他主机／控制端组合需各自验证。

## 检查与移除

```bash
./scripts/build-compat.sh build/compat
./scripts/test-terminal-bridge.sh
```

隔离测试用临时 Wine 前缀，验证没有 service 变量时的 `0600` 交接、错误 token 拒绝、原生 shell、中文 UTF-8、home 和 `24x80` PTY。

```bash
./scripts/verify.sh --quick
systemctl --user status uu-remote-bridge.service --no-pager
tail -n 30 ~/.local/state/uu-remote-bridge/terminal-bridge.log
```

日志只含 readiness、session/尺寸、拒绝和关闭。uninstall 只移除与已安装代理逐字相等的文件；`--purge` 另移除专用前缀。未来 UU 若改变启动协议，按 digest/版本检查处理。只重启桥接会短暂断开 UU，不注销 GNOME、重启 XRDP 或关闭桌面应用。

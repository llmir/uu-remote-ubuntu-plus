[English](native-ubuntu-terminal.md) · [中文（简体）](i18n/zh-Hans/native-ubuntu-terminal.md)

[← English home](../README.md)

# Native Ubuntu terminal through UU Remote

## Result

The UU controller's **Terminal** feature can open the bridge host's real
Ubuntu login shell. The controller may still label the choice `PowerShell`;
on this Wine-hosted Ubuntu device that label is a compatibility entry point,
not the shell that ultimately runs.

Choose `PowerShell` in the UU terminal panel. The resulting PTY runs as the
same unprivileged Ubuntu user as `uu-remote-bridge.service`, starts in that
user's home directory, loads the normal login-shell configuration, supports
interactive programs and UTF-8, and follows controller resize events.

Resize updates accept 1–1000 columns and 1–1000 rows. A well-formed update
outside that range is ignored, preserving the current PTY size and session;
a later valid update is applied normally. This handles controller startup
sequences such as 120×9001 followed by 120×30. Malformed frames and actual
`TIOCSWINSZ` failures still close the affected terminal session.

### Controller version compatibility

Terminal availability depends on the controller platform, UU app version and host terminal protocol. If the client reports `Client version too low` or a streamer error before the shell opens, record both versions, check the official controller's Terminal entry and try a fresh terminal session. Confirm the connection with actual Ubuntu shell output.

Session-list queries can initialize UU transport. Remote command and file-transfer tools should preserve exit status and exact data; use the native terminal for interactive commands and a verified SSH/scp route for files.

## Terminal closes with exit code 0

Wine's `powershell.exe` can be a compatibility placeholder that returns success immediately without opening a shell. The installed `uu-terminal-proxy.exe` handles UU's PowerShell entry and forwards it to Ubuntu's native PTY. If the terminal closes immediately, reinstall the current bridge and check the terminal broker and installed proxy. The `cmd` option opens Wine's diagnostic command processor.

## Data path

```text
UU controller Terminal panel
        |
        | authenticated UU terminal channel
        v
GameViewerServer / conpty_bridge.exe under Wine
        |
        | launches bin/powershell.exe
        v
uu-terminal-proxy.exe
        |
        | random token + framed I/O, 127.0.0.1 only
        v
uu-terminal-bridge
        |
        | forkpty()
        v
Ubuntu user's interactive login shell
```

The Windows proxy is deliberately small. It forwards standard input and
output and reports terminal-size changes. The native broker owns PTY creation,
so commands execute in Ubuntu rather than in Wine.

## Why this does not use SSH

For normal `ssh`, `scp`, `rsync`, or a two-computer return path, use the separate
[SSH and port-mapping workflow](ssh-and-port-mapping.md). It reuses UU's native
TCP forwarding and does not replace or modify this terminal implementation.

SSH would add a second long-lived authentication path, a private key to
manage, and an avoidable network listener. UU has already authenticated the
controller and delivered the terminal stream to the local host. The bridge
therefore continues that stream through an ephemeral localhost socket instead
of logging back into the same computer.

Each service start generates a new 256-bit token. UU rebuilds a default user
environment when it launches the terminal through `CreateProcessAsUser`, so
the proxy cannot rely on inheriting service-only variables. The launcher
instead writes the port and token to `uu-terminal-bridge.runtime` beside the
audited proxy with mode `0600`, then removes it during bridge shutdown. The
ready file still contains only the port, and the token is absent from command
lines. A stale file cannot authenticate after its one broker exits and is
atomically replaced at the next start.

The native listener binds only to IPv4 loopback, compares the complete token,
accepts at most four sessions, and logs session metadata—not commands, output,
or terminal text. It grants no root access and does not bypass `sudo`.

## Prompt rendering compatibility

At a narrow UU terminal width, the final `$` may wrap onto another row because the controller counts title, color and Readline controls differently. The bridge uses `TERM=xterm-256color` and the configured login shell to preserve cursor placement and interactive input. If typed characters appear at the wrong position, close the terminal and open a fresh session. Keep the normal shell configuration for local terminals.

## Install and use

For a terminal-to-terminal connection, use the short helper on either bridge.
The peer profile explicitly selects `terminal` or `ssh`; the helper never tries
one and silently falls back to the other:

```bash
# Native Terminal is the default for old and newly created profiles.
uu-ssh add lab --port 22709 --user YOUR_REMOTE_USER \
  --device-id UU_DEVICE_ID --shell-transport terminal
uu-shell lab

# Explicitly resume a known vendor session instead of opening a fresh one:
uu-shell lab --session-id SESSION_ID

# A verified mapped-SSH profile accepts normal ssh arguments/commands instead:
uu-ssh add lab --port 22709 --user YOUR_REMOTE_USER --shell-transport ssh
uu-shell lab 'hostname; id -un'
uu-shell --help
```

For `terminal`, it delegates to the existing native Terminal adapter and opens
a fresh session by default. For `ssh`, it executes the profile's pinned
`uu-PEER` OpenSSH alias. Both paths pass arguments literally and preserve the
selected transport's exit status and normal I/O/signals. It does not start a
daemon, open a desktop, activate Port Mapping, retry, or fall back to another
route. The helper itself does not fix controller-compatibility failures or a
closed mapping; acceptance still requires a real remote shell. Session-list
requests are explicit and can initialize vendor transport. File transfer
remains a separate acceptance test; use a verified mapped SSH/scp path, not
pasted terminal text as an unverified file channel.

To update only the shell/SSH entry points without restarting the desktop:

```bash
install -m 0755 scripts/uu-ssh scripts/uu-shell "$HOME/.local/bin/"
```

The installer, upgrade backup and uninstaller also include `uu-shell`.

The normal installer builds and deploys both helpers:

```bash
./install.sh --skip-packages --skip-account-login
```

It replaces `GameViewer/bin/powershell.exe` only when that path is absent or
already contains this repository's installed proxy. An unexpected vendor or
user file causes installation to fail closed. The bridge service supervises
the native broker and restarts the complete UU stack if it exits.

In the remote UU client:

1. Open the Ubuntu device.
2. Choose **Terminal**.
3. Leave the shell on **PowerShell**.
4. Run Ubuntu commands normally; use `exit` to close that terminal.

The `cmd` choice remains Wine's diagnostic command processor. Use the
`PowerShell` choice for the native Ubuntu shell.

## Keep the terminal across reconnects (opt-in)

The default `UURB_TERMINAL_SESSION_MODE=fresh` opens a new login shell and
ends it on disconnect. After installing a build containing this feature,
enable persistence in the existing service environment file:

```bash
sed -i '/^UURB_TERMINAL_SESSION_MODE=/d' ~/.config/uu-remote-bridge/environment
printf '%s\n' 'UURB_TERMINAL_SESSION_MODE=persistent' >> ~/.config/uu-remote-bridge/environment
```

The setting takes effect at the next controlled bridge restart (`uu-remote
restart` disconnects the desktop too). Installation preserves the saved
setting; it does not enable persistence by default. To return to fresh mode,
use the same commands with `fresh` instead of `persistent`, then apply at the
next controlled restart. No restart is performed just by saving the setting.

Persistence uses the existing `/usr/bin/tmux`, an independent socket at
`${XDG_RUNTIME_DIR:-/run/user/$UID}/uu-remote-bridge/terminal-tmux.sock`, and a
workspace named `main`. It does not load your tmux configuration or use your
normal tmux server. All UU terminal windows share `main`: input, shell state,
and the workspace are shared. Disconnecting closes only the attach client;
reconnecting returns to the same shell, working directory and running jobs.
Transport EOF also detaches; typed Ctrl-D and `exit` remain shell input.
Resize still travels through the existing PTY protocol. The account's home,
shell and login configuration are retained; transport token and port are
removed before starting tmux. If tmux cannot be executed, the terminal reports
that it is falling back to fresh mode; no dependency is installed.

Use `Ctrl-b d` to detach. `exit` closes the current shell; `tmux kill-session
-t main` inside this workspace ends the entire workspace and its jobs. The
next connection creates a new `main`. Persistence covers terminal disconnects,
not logout, reboot, or a full bridge-service restart.

Run the short native-only probe with `python3 scripts/test-terminal-persistence.py`.
It compiles a helper in a temporary directory and uses only its own tmux socket.
Actual UU-controller reconnect behavior still requires controller acceptance.

## Current acceptance · 2026-10-06

A real Mac UU close/reopen cycle on the current source retained the same shell,
working directory and background job in `persistent` mode. Ordinary input,
Ctrl-C, resize and clear passed. Rapid batch input still has an unresolved
boundary; the accepted ordinary-input result must not be extended to that case.
These observations used Ubuntu 26.04 / GNOME 50, Wine 11 and Windows UU
4.42.0.2770. Other host/controller combinations require their own check.

## Verification

The isolated test creates a disposable Wine prefix and never touches the
logged-in desktop:

```bash
./scripts/build-compat.sh build/compat
./scripts/test-terminal-bridge.sh
```

It launches the proxy without either service environment variable, proving
the same mode-0600 runtime-file handoff used by UU's reconstructed user
environment. It also proves that an incorrect token is rejected and that the
accepted path delivers an interactive native shell, exact UTF-8 Chinese, the
user's home directory, and a `24x80` PTY.

For the installed service:

```bash
./scripts/verify.sh --quick
systemctl --user status uu-remote-bridge.service --no-pager
tail -n 30 ~/.local/state/uu-remote-bridge/terminal-bridge.log
```

The log intentionally contains only readiness, session PID/size, rejection,
and close events.

## Removal and compatibility

`./uninstall.sh` removes the compatibility executable only when it is
byte-identical to the installed proxy. It refuses to remove an unknown file.
`./uninstall.sh --purge` additionally removes the dedicated Wine prefix as
described in the main removal guide.

The bridge targets the audited UU host behavior in this repository. A future
UU release may change the executable name or terminal launch contract; normal
runtime-digest and release checks should reject drift rather than guessing.
Updating or restarting only `uu-remote-bridge.service` briefly disconnects UU
but does not log out GNOME, restart XRDP, or close the user's desktop apps.

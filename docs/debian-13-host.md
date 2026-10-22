# Experimental Debian 13 host support

This port targets x86-64 Debian 13 (trixie), GNOME 48 and a logged-in,
unlocked desktop. Another device running the official UU client controls
this Debian host. Keep GNOME desktop sharing active during use.

The port is experimental. Debian acting as a controller, unattended startup,
locked sessions, display-off operation and hibernation are outside the current
acceptance record. Locking the desktop or stopping GNOME sharing can terminate
the EIS/RDP session; service recovery does not establish lock-screen support.

## First installation

Run the installer from a terminal in the target GNOME desktop:

```bash
./install.sh
```

Use the ordinary desktop user, with permission to install dependencies through
`sudo`. Do not run the whole installer as root. If an administrator has already
installed the dependencies declared by `install.sh`, use `--skip-packages`.
The installer asks for a local RDP relay password and stores it in GNOME Keyring.
Never include that password or UU account state in an issue or pull request.

For a new Debian configuration, the installer:

- Selects the existing `legacy` SDL/RDP input route. GNOME 48 does not advertise
  the relative-pointer capability required by `rdp-public`.
- Enables the process-local cursor guard with automatic theme sizing. Saved
  settings and explicit `--cursor-guard off` / `UURB_CURSOR_GUARD=off` choices
  take precedence. Ubuntu's new-install default remains off.
- Sets the per-application GDI renderer for `GameViewer.exe` in a new dedicated
  Wine prefix, before the official installer can launch that manager. This
  avoids the OpenGL pixel-format initialization hang seen on the tested host.
- Starts the native account window with the Wine desktop helper, then waits
  for confirmation in the terminal. Finish UU sign-in, return to the terminal
  and press Enter. Only then does installation stop that prefix and start the
  bridge. The launcher's early exit cannot end this sign-in session; EOF or
  cancellation aborts the sign-in step and cleans up the dedicated prefix.

An already configured prefix retains its renderer and account state. For an
older Debian installation that saved the previous cursor default, explicitly
opt in when reinstalling:

```bash
./install.sh --skip-packages --skip-account-login \
  --cursor-guard on --cursor-size auto
```

The defaults above are Debian-specific. The Ubuntu 24.04/26.04 installation
branches retain their existing behavior.

## Dependencies and isolated relay build

The installer uses WineHQ's Debian trixie repository, the existing
`/opt/wine-stable/bin/` runtime path, and the matching `wine-stable-dev` headers
and Winelib import libraries. Compatibility components build on Debian.

For a cold relay build, package preparation installs Podman and uidmap even if
a Docker command is present. A Docker CLI alone does not establish a usable,
supported daemon. The reference compiler and relay are built inside Ubuntu
26.04; Ubuntu reference packages are never installed into Debian's `/usr`.
Valid cached relay output or an explicitly supplied prebuilt output avoids
this extra container dependency installation.

The container calls the existing source build and verifies the original tool
identities, source locks, thirteen PE runtime files and provenance. Any failed
check stops installation. Download and compilation time depends on the network
and available CPU.

Preview the commands without downloading or modifying files:

```bash
python3 scripts/build-reference-relay.py --dry-run --engine podman
```

With Podman/uidmap available, build and verify the relay independently:

```bash
python3 scripts/build-reference-relay.py
python3 scripts/verify-freerdp-runtime.py build/freerdp
```

The image build context contains only recipes and public manifests. The
checkout is mounted read-only. Working files go in `build/reference-relay-work`
and output in `build/freerdp`, owned by the desktop user. Existing approved
output is reused. The upstream prebuilt interface also remains available:

```bash
UURB_FREERDP_PREBUILT_DIR=/absolute/path/to/verified/output ./install.sh
```

Rootless Podman requires subordinate UID/GID mappings in `/etc/subuid` and
`/etc/subgid`. Direct Docker builds are available with `--engine docker` only
for a usable daemon without rootless mode or user-namespace remapping. Those
Docker configurations are rejected to preserve bind-mount ownership; use
Podman instead.

Loopback proxy settings select host networking for image build and source
compilation. Proxy values pass through the environment, and command previews
omit credentials. Resolve container/network errors without disabling product
verification or installing Ubuntu packages on the Debian host.

## Version-locked compatibility changes

The approved UU 4.42 installer now uses NetEase's fixed CDN URL from the
[Microsoft WinGet manifest](https://github.com/microsoft/winget-pkgs/blob/master/manifests/n/NetEase/UURemote/4.42.0.2770/NetEase.UURemote.installer.yaml).
The original complete SHA-256 and binary patch definitions are unchanged.
The moving latest-release endpoint can return a different, unapproved version.

The shared FreeRDP build receives a `lib`-prefixed PE export forwarder to the
canonical input plugin. API resolution accepts the already loaded core DLL
only when it is colocated with the main executable. The static resolution path
and fixed thirteen-file relay closure remain intact. Public input readiness
still fails closed if required capabilities are missing.

ConPTY declarations explicitly select the Windows 10 RS5 API level for the
Debian MinGW headers. An empty registry-cleanup plan emits no deletion entry,
and tool assets create their destination directories on a fresh user account.

## Validation and remaining limits

```bash
./scripts/verify.sh --quick
./scripts/verify.sh
```

The full verifier includes a 270-second server/relay stability interval.
Connect from the official UU client on another device and check the picture,
clicking, dragging, scrolling, modifier keys, text entry, clipboard actions and
reconnection. Start at 1080p before trying other canvas presets.

The local Debian 13 / GNOME 48 acceptance record includes real controller
connections, visible cursor and clicking, ASCII and Chinese input, in-session
text copy/paste, and reconnection. A prior ten-minute test observed no service
restart or relay descriptor growth. It also observed occasional stutter and
LAN latency spikes; no end-to-end frame-rate claim is made.

Automated coverage includes distribution selection, fresh Wine bootstrap
ordering, account-login confirmation/EOF handling, cursor defaults and explicit
choices, unsupported Docker fallback, host SDK compilation and real Wine
shared-core resolution. Fresh-prefix installation and account-window startup
are checked separately from the existing authenticated host.

The local TigerVNC management window previously had mouse interaction problems.
The inherited standalone `uu-remote login` wrapper has a separate launcher
lifecycle limitation; it is not a validated reauthentication path. The Debian
installer's native first-sign-in path above has its own confirmation handling.
Ubuntu desktop acceptance and other controller/platform combinations require
separate testing. Passing source tests does not establish full Debian support.

For an existing prefix affected by the manager splash hang, the equivalent
application-scoped renderer command is:

```bash
WINEPREFIX="$HOME/.local/share/wineprefixes/uu-remote" WINEDEBUG=-all \
  /opt/wine-stable/bin/wine reg add \
  'HKCU\Software\Wine\AppDefaults\GameViewer.exe\Direct3D' \
  /v renderer /t REG_SZ /d gdi /f
```

## Stop and uninstall

```bash
uu-remote stop
./uninstall.sh --dry-run
./uninstall.sh
```

Ordinary removal preserves UU account state in the dedicated Wine prefix.
Container images and ignored build caches are separate from the running bridge
and can be managed through the container engine and `build/` directory.

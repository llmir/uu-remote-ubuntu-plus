<div align="center">

[English](README.md) · [العربية](i18n/README.ar.md) · [Español](i18n/README.es.md) · [Français](i18n/README.fr.md) · [日本語](i18n/README.ja.md) · [한국어](i18n/README.ko.md) · [Tiếng Việt](i18n/README.vi.md) · [中文 (简体)](i18n/README.zh-Hans.md) · [中文（繁體）](i18n/README.zh-Hant.md) · [Deutsch](i18n/README.de.md) · [Русский](i18n/README.ru.md)

<a href="README.md"><img src="docs/images/uu-plus-logo.png" alt="UU Remote Ubuntu Plus" width="140"></a>

# UU Remote Ubuntu Plus

**Stay in the flow. Keep your development environment on Ubuntu.**

Keep your editor, terminals and app sessions on Ubuntu. Connect from your phone, Mac or Windows computer for Vibe Coding whenever inspiration strikes, then switch screens and keep writing.

[![Ubuntu 24.04 / 26.04](https://img.shields.io/badge/Ubuntu-24.04%20%7C%2026.04-E95420?logo=ubuntu&logoColor=white)](docs/ubuntu-26.04-port.md)
[![GNOME 46 / 50](https://img.shields.io/badge/GNOME-46%20%7C%2050-4A86CF?logo=gnome&logoColor=white)](docs/architecture.md)
[![UU Remote 4.42](https://img.shields.io/badge/UU_Remote-4.42.0.2770-00A870)](patches/uu-remote-4.42.0.2770.json)
[![MIT](https://img.shields.io/badge/License-MIT-2F81F7)](LICENSE)

<img src="docs/images/vibe-coding-cross-device-en.png" alt="UU Remote Ubuntu Plus" width="1120">

[Install](#quick-install) · [Features](#features-and-improvements) · [How it works](#technical-overview) · [Quality](#resolution-and-quality) · [Support](#support-the-project)

</div>

Plus connects NetEase UU Remote to your logged-in Ubuntu GNOME desktop. It runs
the official Windows UU application in a dedicated Wine environment, with a
local relay for the real desktop and a separate window for UU account and
settings controls. Your applications, files and desktop session stay together
as you move between local and remote use.

The project builds on **[UU Remote Ubuntu Bridge by Lachlan Chen](https://github.com/lachlanchen/uu-remote-ubuntu-bridge)**.
Plus brings newer Ubuntu and UU compatibility, four canvas presets, input and
clipboard corrections, and clearer management and desktop controls.

The installer targets x86-64 Ubuntu 24.04 / GNOME 46 and Ubuntu 26.04 / GNOME 50, using UU 4.42.0.2770. New installations select 1080p with the optional cursor guard off.

The eleven language pages and technical guides describe Plus in each language; English is the canonical reference.

## What's new in 0.2.0-work

This development version adds terminal session persistence, multi-file reception
and image-format compatibility work, with optional native capture experiments.

- **Keep your terminal workspace:** enable `persistent` mode to return to the
  same Ubuntu shell, directory and jobs after reconnecting. Fresh sessions
  remain the default. [Set up terminal persistence](docs/native-ubuntu-terminal.md#keep-the-terminal-across-reconnects-opt-in).
- **Receive several files at once:** copy regular files on the controller and
  paste the completed batch into an Ubuntu file manager. The receive window
  shows file and batch progress in actual bytes. [File reception](docs/releases/plus-v0.2.0-work.md#incoming-multi-file-copy).
- **Preserve image formats:** retain original PNG with DIBV5/DIB offers. The
  optional Mac companion adds TIFF for PNG compatibility. [Mac helper](docs/macos-clipboard-compat.md).
- **Explore native capture:** optional CPU/GPU prototypes investigate capture
  paths beyond the RDP relay. These remain experimental. [Developer guide](docs/native-video-backends.md).

<img src="docs/images/uu-plus-update-20261006-en.png" alt="0.2.0-work features: persistent terminal workspace, multi-file reception with byte progress, image compatibility and experimental native capture." width="1120">

File reception supports incoming regular files. Current Ubuntu → Mac image
synchronization and dual-controller focus remain open. [Known limits and working-version notes](docs/releases/plus-v0.2.0-work.md).

[Update notes and announcement](docs/updates/2026-10-06.md) · [Editable SVG](docs/images/uu-plus-update-20261006-en.svg)

## From setup to remote work

1. Install the bridge on your Ubuntu desktop.
2. Run `uu-remote open` and sign in to UU in the local management window.
3. Connect to this Ubuntu host from your phone, Mac or Windows UU controller.
4. Choose a canvas size and use your regular desktop applications.

## Features and improvements

Upstream provides the desktop relay, keyboard and mouse input, phone IME handling and service recovery. Plus builds on that foundation with newer compatibility, clearer quality controls and refinements for everyday use.

| Everyday use | What Plus adds |
| --- | --- |
| Newer Ubuntu and UU | Ubuntu 26.04 / GNOME 50 and UU 4.42 compatibility, with the Ubuntu 24.04 installation path retained. |
| Pick your canvas | Switch between 720p, 1080p, 1440p and 4K in a graphical selector. Fit the full desktop into the canvas and restore the saved setting. |
| Chinese, code and copy/paste | Repairs phone text commits and clipboard refresh, so new text reaches the desktop and code snippets and multiline text keep their content. |
| Open settings. Stay connected. | Capture the UU manager and its popups separately. Dual-controller focus switching remains under investigation. |
| Better local tools | Launch quality settings, VNC, FreeRDP and Openbox tools, with font, DPI and startup refinements. |
| Install and maintain | Build the relay from pinned source and check runtime components before installation. Recover failed canvas changes and preview removal before uninstalling. |

See the [upstream comparison](docs/upstream-comparison.md) for specific changes and version context.

<img src="docs/images/experience-refinements-en.png" alt="Experience and measurements" width="1120">

[Experience and measurements](docs/performance-evidence.md)

## Quick install

An **experimental Debian 13 / GNOME 48 host port** is also available. Its
installer uses Debian WineHQ packages and builds the pinned relay inside
Ubuntu 26.04 with Podman. New Debian setups select the compatible input route,
enable the cursor guard and prepare the manager's GDI renderer before first
sign-in. See the [Debian host guide](docs/debian-13-host.md) for setup and limits.

Use an x86-64 Ubuntu host with a logged-in GNOME desktop. Clone the project:

```bash
git clone https://github.com/llmir/uu-remote-ubuntu-plus.git
cd uu-remote-ubuntu-plus
```

Installation needs the reviewed toolchain to build the relay, or an existing verified output matching the product profile; relay binaries are not included. Ubuntu 26.04 provides the reference-tool preparation path; Ubuntu 24.04 uses verified output reuse. [Toolchain preparation and relay reuse](docs/source-build.md#reference-toolchain)

Once the tools or matching output are ready, run the normal installer:

```bash
./install.sh
./scripts/verify.sh --quick
uu-remote open
```

The installer prepares dependencies, builds compatibility components, configures
GNOME Remote Desktop and starts user services. It asks for a relay password,
stores it in GNOME Keyring and opens UU for account sign-in. Reinstallation
retains saved settings and the existing UU account state.

New installations use 1080p and keep the optional cursor guard off. To start at 4K:

```bash
./install.sh --resolution 3840x2160
```

Report your environment and reproducible behavior through the [compatibility report form](https://github.com/llmir/uu-remote-ubuntu-plus/issues/new?template=compatibility.yml).

The first installation may download and compile source dependencies. See
[Source build](docs/source-build.md) for requirements and
[Security](docs/security.md) for the isolation design.

Get the Windows UU client from NetEase's official [uuyc.163.com](https://uuyc.163.com/) site. The bridge runs it in a dedicated Wine environment. UU retains its own license.

## Technical overview

<img src="docs/images/architecture-premium-v2-en.png" alt="Ubuntu desktop image, input and local UU management paths." width="1120">

The Ubuntu desktop travels through GNOME RDP into the SDL / FreeRDP relay, then through UU to the controller. Keyboard and mouse input return through the input bridge to the same desktop session. UU account and settings controls have their own local management window.

Run `uu-remote open` for the manager; the desktop bridge keeps running when the viewer closes. The optional `uu-remote console` provides a local browser view. See [Architecture](docs/architecture.md) for the modules and input paths.

## Resolution and quality

<img src="docs/images/quality-controls-cartoon-v2-en.png" alt="Ubuntu canvas and UU controller quality controls." width="1120">

Open **UU Remote 画质与分辨率** from the GNOME application list, or run:

```bash
uu-remote quality gui
```

The full source desktop fits inside the canvas, while the physical monitor's
resolution stays unchanged. Applying another preset briefly reconnects the
bridge; a failed change restores the previous configuration.

```bash
uu-remote quality list
uu-remote quality apply 2160p
uu-remote quality status
uu-remote quality guide
```

Set UU encoding quality, FPS and True Color in the **controller**: **Control
Center → Quality** on a computer, or **Operations → Display** on a phone.

The bridge also provides a bitrate ceiling:

```bash
uu-remote quality bitrate 20
uu-remote quality bitrate 0
```

`20` requests a 20 Mbps ceiling; `0` removes it. Canvas size, encoding quality,
requested FPS and bitrate are separate controls. See the
[quality guide](docs/quality-guide.md) for practical choices.

## Keyboard, clipboard and cursor

Phone IME commits, code snippets and multiline text use the text path into Ubuntu. Physical keys and shortcuts retain their key events. Plus repairs text commits and clipboard refresh for everyday typing and text copy/paste. See [Adaptive keyboard relays](docs/adaptive-keyboard-relays.md) for the input modes.

The optional cursor guard comes from upstream. Plus adds cursor asset handling
and related corrections. To enable it for the dedicated UU Wine environment:

```bash
./install.sh --skip-packages --skip-account-login   --cursor-guard on --cursor-size auto
```

`auto` follows the desktop cursor size. A fixed value such as `24` controls the
guard's fallback cursor size. Use `--cursor-guard off` to turn it off.
Reinstallation briefly reconnects UU.

## Ubuntu terminal and incoming copies

Open **Terminal → PowerShell** in the UU controller to reach the real Ubuntu
login shell. Opt in to `UURB_TERMINAL_SESSION_MODE=persistent` to reopen the same
workspace; the [terminal guide](docs/native-ubuntu-terminal.md#keep-the-terminal-across-reconnects-opt-in)
explains applying the setting at a planned reconnect and how `exit` closes it.

For incoming files, copy regular files in the controller's file manager and
paste into Ubuntu's file manager. **UU 文件接收** shows the file/batch byte
progress. The completed batch is staged before it becomes pasteable; failed or
cancelled unfinished batches are removed. Each file is limited to 64 MiB, with
up to 64 files and 256 MiB per batch. Completed copies remain in the staging
folder until you remove them. See the [clipboard notes](docs/releases/plus-v0.2.0-work.md#clipboard-images-and-text).

Images use a separate compatibility path. The [optional Mac PNG helper](docs/macos-clipboard-compat.md)
adds TIFF while preserving PNG. It is a local opt-in app, not a built-in UU
feature or a guarantee that every current image route works.

## Daily use and maintenance

```bash
uu-remote status
uu-remote network
uu-remote logs
uu-remote restart
uu-remote stop
```

Use `uu-remote open` for the management window and `uu-remote login` for account
sign-in or recovery. Restarting, logging in and reinstalling briefly disconnect
the controller.

For source updates, save local edits, update the checkout and run:

```bash
./install.sh --skip-packages --skip-account-login
./scripts/verify.sh --quick
```

Runtime source changes take effect after reinstallation. See
[Reusable upgrade](docs/reusable-upgrade.md) for `uu-remote upgrade`, and
[Automatic updates](docs/automatic-updates.md) for optional maintenance.

To remove the bridge while retaining the dedicated UU account state:

```bash
./uninstall.sh --dry-run
./uninstall.sh
```

`./uninstall.sh --purge` additionally removes the dedicated Wine prefix,
relay credential and GNOME RDP enablement.

## Beyond Ubuntu

The current installer targets x86-64 Ubuntu 24.04 and 26.04. The [Linux porting guide](docs/porting.md) separates future ports into three layers:

- Reuse the UU compatibility, relay and input core.
- Adapt distribution packages, Wine paths and service integration.
- Connect desktop-specific capture, input, display modes and actions.

Other GNOME distributions can reuse more of the current desktop integration. KDE and Xfce ports need their own desktop backends.

## Documentation and contributions

- [October development update](docs/updates/2026-10-06.md), [Native terminal](docs/native-ubuntu-terminal.md) and [Mac image compatibility](docs/macos-clipboard-compat.md)
- [Quality and resolution](docs/quality-guide.md)
- [Source build](docs/source-build.md) and [Ubuntu 26.04 port](docs/ubuntu-26.04-port.md)
- [Architecture](docs/architecture.md) and [Security](docs/security.md)
- [Upstream comparison](docs/upstream-comparison.md) and [Measurements](docs/performance-evidence.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Changelog](CHANGELOG.md) and [Contributing](CONTRIBUTING.md)

Describe the version, selected settings and reproducible behavior.

## Support the project

**Buy me a coffee ☕**

UU and Ubuntu keep changing. I’ll keep working to make Plus fit new releases and make text input, copy and paste, and picture quality feel right. A coffee helps cover version testing, development tools and tokens.

| PayPal | Alipay · CNY | AlipayHK · HKD | WeChat · CNY | WeChat · HKD |
| :---: | :---: | :---: | :---: | :---: |
| <a href="https://paypal.me/mirmirlin"><img src="docs/images/support-paypal-en.png" alt="PayPal" width="160"></a> | <a href="docs/images/support/alipay-cny.jpg"><img src="docs/images/support-alipay-cny-en.png" alt="Alipay · CNY" width="160"></a> | <a href="docs/images/support/alipay-hkd.png"><img src="docs/images/support-alipay-hkd-en.png" alt="AlipayHK · HKD" width="160"></a> | <a href="docs/images/support/wechat-zh.png"><img src="docs/images/support-wechat-zh-en.png" alt="WeChat · CNY" width="160"></a> | <a href="docs/images/support/wechat-en.png"><img src="docs/images/support-wechat-en-en.png" alt="WeChat · HKD" width="160"></a> |

<details>
<summary>Alipay and WeChat payment codes</summary>

<p><a href="docs/support.md#alipay-cny">Alipay CNY</a><br><a href="docs/images/support/alipay-cny.jpg"><img src="docs/images/support/alipay-cny.jpg" alt="Alipay CNY" width="240"></a></p>

<p><a href="docs/support.md#alipay-hkd">AlipayHK HKD</a><br><a href="docs/images/support/alipay-hkd.png"><img src="docs/images/support/alipay-hkd.png" alt="AlipayHK HKD" width="240"></a></p>

<p><a href="docs/support.md#wechat-zh">WeChat · CNY</a><br><a href="docs/images/support/wechat-zh.png"><img src="docs/images/support/wechat-zh.png" alt="WeChat · CNY" width="240"></a></p>

<p><a href="docs/support.md#wechat-en">WeChat · HKD</a><br><a href="docs/images/support/wechat-en.png"><img src="docs/images/support/wechat-en.png" alt="WeChat · HKD" width="240"></a></p>

</details>

Reproducible bug reports, Linux porting notes and pull requests are welcome too. Thanks for helping shape the next version.

[Support UU Remote Ubuntu Plus](docs/support.md)

## Credits and license

Based on **[UU Remote Ubuntu Bridge by Lachlan Chen](https://github.com/lachlanchen/uu-remote-ubuntu-bridge)**.
The original copyright notice and [MIT license](LICENSE) are preserved.
UU Remote and the other dependencies retain their own licenses and trademarks.
This is an independent community project.

Payment brand icons: [Simple Icons](https://simpleicons.org/) (CC0); third-party brands retain their trademarks.

The optional GPU capture component has its own [AGPL-3.0 license and source notices](vendor/uuway-gpu-component/README.md); the MIT CPU adapter has [separate attribution](vendor/uur-native-cpu/NOTICE). These prototypes are outside the default installer.

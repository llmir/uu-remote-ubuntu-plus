[English](CHANGELOG.md) · [العربية](i18n/CHANGELOG.ar.md) · [Deutsch](i18n/CHANGELOG.de.md) · [Español](i18n/CHANGELOG.es.md) · [Français](i18n/CHANGELOG.fr.md) · [日本語](i18n/CHANGELOG.ja.md) · [한국어](i18n/CHANGELOG.ko.md) · [Русский](i18n/CHANGELOG.ru.md) · [Tiếng Việt](i18n/CHANGELOG.vi.md) · [简体中文](i18n/CHANGELOG.zh-Hans.md) · [繁體中文](i18n/CHANGELOG.zh-Hant.md)

[Home](README.md)

# Changelog

Bridge release tags and the approved Windows UU version are tracked separately.

## Unreleased

- Experimental Debian 13 / GNOME 48 host setup, using Debian WineHQ packages
  and a Podman build of the pinned Ubuntu reference relay.
- Debian first-sign-in preparation: application-scoped GDI rendering,
  compatible input and cursor defaults, and terminal confirmation before
  retiring the native account window. Explicit saved settings are preserved.
- Fresh-user tool asset installation, empty Wine device-registry cleanup,
  MinGW ConPTY declarations and shared FreeRDP input-plugin loading fixes.
- Fixed official UU 4.42 installer URL; approved hashes and patches are unchanged.

## Plus 0.2.0-work — 2026-10-06

Development source update; Plus 0.1.0 remains the last tagged release.

- Optional persistent Ubuntu terminal workspace across reconnects; raw terminal
  I/O and session handoff improvements. Fresh sessions remain the default.
- Incoming multi-file clipboard reception, with actual byte progress,
  cancellation and interrupted-batch cleanup. Regular files only.
- Original PNG preservation alongside DIBV5/DIB image offers, and a single native
  clipboard authority.
- Optional Mac PNG → TIFF companion with a local settings window.
- Optional native capture experiments: MIT CPU adaptation and an independent
  AGPL-3.0 GPU component. No real-session performance gain is claimed.
- Updated homepages and changelogs in eleven languages, with bilingual update
  illustrations and usage links.

Known limits: rapid batch terminal input, current Ubuntu → Mac image acceptance
and dual-controller focus remain open. Outgoing files and directory copies are
not implemented. Recent focus/public-input trial candidates are excluded.
[Update notes](docs/updates/2026-10-06.md) · [Working-version details](docs/releases/plus-v0.2.0-work.md)

## Plus 0.1.0 — 2026-10-03

Plus builds on the MIT-licensed upstream bridge.

### Added

- Desktop size: Four visible presets up to 4K, complete-desktop fitting and saved-size recovery.
- Phone text: Plus input-path refinements and the public FreeRDP text route.
- Management: Independent window/popup capture and reusable viewer sessions.
- Cursor: Theme-based fallback and startup fixes; default remains off.
- Desktop actions: GNOME desktop/overview action mapping.
- Desktop tools: Quality/VNC/FreeRDP/Openbox launchers with scoped fonts, DPI and credentials.
- Build and recovery: Pinned patched source build, runtime checks and preset/viewer recovery.
- Eleven-language homepages and core guides, with editable architecture, porting and comparison graphics.

### Fixed

- Clipboard: SDL source patches for background checks, owner changes and cached formats.
- Management-window reopening, UTF-8 titles, owned popups and focus return when the viewer closes.
- Phone text submission, input-hook initialization, partial input handling and bounded native-terminal shutdown.
- Complete-canvas pointer mapping, independent size/bitrate settings, protected manual RDP sessions and reversible tool launchers.

### Compatibility

- Ubuntu 24.04 / GNOME 46 · Ubuntu 26.04 / GNOME 50 · x86-64.
- Windows UU: Approved 4.42.0.2770 as the default.

## Inherited upstream — unreleased

Upstream provides semantic Unicode clipboard transactions, composition editing and long dictation batches, physical keyboard layouts, authenticated host input, network/runtime diagnostics, Wine Bluetooth-driver isolation and unattended-session recovery.

## Upstream 0.2.0 — 2026-07-18

- Network diagnostics and installed-source digest; selectable default/fixed network adapter without replacing the host route.
- GNOME RDP descriptor recovery and libei backport; descriptor limits, session Keyring readiness and system Python/GI handling.
- Configurable phone text pacing and zero-delay physical keys; original `SendInput` first, broker fallback and confirmed relay focus.
- Authenticated X11/XTEST input and categorized telemetry; cleanup of replaced Xvfb/relay sessions and debounced network recovery.
- XRDP keyboard-layout/reconnect guidance and unattended-session documentation.

[Original 0.2.0 release](https://github.com/lachlanchen/uu-remote-ubuntu-bridge/releases/tag/v0.2.0).

## Upstream 0.1.0 — 2026-07-17

- First supported Wine/UU bridge with Xvfb, SDL FreeRDP, GNOME relay, user input broker, reinjection and supervised service.
- Saved resolution/port/display configuration, audited binary changes, rollback and normal RDP clipboard support.
- Optional TPM2/GDM unattended startup.
- Phone Unicode/virtual-key normalization, session D-Bus discovery across Wayland/Xorg/XRDP, Wine event-log compatibility and old-prefix cleanup.

[Original 0.1.0 release](https://github.com/lachlanchen/uu-remote-ubuntu-bridge/releases/tag/v0.1.0). [Complete pinned upstream history](https://github.com/lachlanchen/uu-remote-ubuntu-bridge/blob/e2854e2b/CHANGELOG.md) includes the original detailed change and validation records.

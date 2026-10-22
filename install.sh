#!/usr/bin/env bash

set -Eeuo pipefail
umask 077

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/runtime-settings.sh
source "$repo_dir/scripts/runtime-settings.sh"
bridge_user="${USER:-$(id -un)}"
config_dir="$HOME/.config/uu-remote-bridge"
environment_file="$config_dir/environment"
saved_wine_prefix="$(
    if [[ -f "$environment_file" ]]; then
        /usr/bin/sed -n 's/^UURB_WINEPREFIX=//p' "$environment_file" | /usr/bin/tail -n 1
    fi
)"
wine_prefix="${UURB_WINEPREFIX:-${WINEPREFIX:-${saved_wine_prefix:-$HOME/.local/share/wineprefixes/uu-remote}}}"
wine_bin='/opt/wine-stable/bin/wine'
wineserver_bin='/opt/wine-stable/bin/wineserver'
grdctl_bin='/usr/bin/grdctl'
openssl_bin='/usr/bin/openssl'
python_bin='/usr/bin/python3'
secret_tool_bin='/usr/bin/secret-tool'
systemctl_user=(
    /usr/bin/env
    "DBUS_SESSION_BUS_ADDRESS=unix:path=${XDG_RUNTIME_DIR:-/run/user/$UID}/bus"
    /usr/bin/systemctl --user
)
uu_dir="$wine_prefix/drive_c/Program Files/Netease/GameViewer"
uu_bin="$uu_dir/bin"
release_manifest="${UURB_RELEASE_MANIFEST:-$repo_dir/patches/uu-remote-4.42.0.2770.json}"
installed_manifest="$wine_prefix/compat/release-manifest.json"
runtime_digest_file="$wine_prefix/compat/.runtime-source-sha256"
server_exe=''
healthd_exe=''
compat_build="$repo_dir/build/compat"
freerdp_build="$repo_dir/build/freerdp"
libei_build="$repo_dir/build/libei"
freerdp_install="$wine_prefix/drive_c/Program Files/FreeRDP"
libei_install="$wine_prefix/compat/libei"
uu_download_url=''
uu_installer_filename=''
uu_installer_sha256=''
healthd_sha256=''
terminal_proxy_install="$uu_bin/powershell.exe"
installed_terminal_proxy="$wine_prefix/compat/uu-terminal-proxy.exe"
saved_setting() {
    local name="$1"

    [[ -f "$environment_file" ]] || return 0
    /usr/bin/sed -n "s/^${name}=//p" "$environment_file" | \
        /usr/bin/tail -n 1
}
saved_rdp_port="$(saved_setting UURB_RDP_PORT)"
saved_resolution="$(saved_setting UURB_RESOLUTION)"
saved_follow_desktop_resolution="$(
    saved_setting UURB_FOLLOW_DESKTOP_RESOLUTION
)"
saved_display="$(saved_setting UURB_DISPLAY)"
saved_desktop_target="$(saved_setting UURB_DESKTOP_TARGET)"
saved_desktop_relay="$(saved_setting UURB_DESKTOP_RELAY)"
saved_input_route="$(saved_setting UURB_INPUT_ROUTE)"
saved_shared_vnc_port="$(saved_setting UURB_DESKTOP_VNC_PORT)"
saved_vnc_grab_keyboard="$(saved_setting UURB_VNC_GRAB_KEYBOARD)"
saved_grd_fd_restart_threshold="$(
    saved_setting UURB_GRD_FD_RESTART_THRESHOLD
)"
saved_text_key_delay_ms="$(saved_setting UURB_TEXT_KEY_DELAY_MS)"
saved_physical_key_delay_ms="$(saved_setting UURB_PHYSICAL_KEY_DELAY_MS)"
saved_keyboard_route="$(saved_setting UURB_KEYBOARD_ROUTE)"
saved_phone_text_mode="$(saved_setting UURB_PHONE_TEXT_MODE)"
saved_network_interface="$(saved_setting UURB_NETWORK_INTERFACE)"
saved_cursor_guard="$(saved_setting UURB_CURSOR_GUARD)"
saved_cursor_size="$(saved_setting UURB_CURSOR_SIZE)"
saved_libei_mode="$(saved_setting UURB_LIBEI_MODE)"
saved_terminal_session_mode="$(saved_setting UURB_TERMINAL_SESSION_MODE)"
saved_console_vnc_port="$(saved_setting UURB_CONSOLE_VNC_PORT)"
saved_console_web_port="$(saved_setting UURB_CONSOLE_WEB_PORT)"
saved_window_scale="$(saved_setting UURB_WINDOW_SCALE)"
rdp_port="${UURB_RDP_PORT:-${saved_rdp_port:-3390}}"
resolution="${UURB_RESOLUTION:-${saved_resolution:-1920x1080}}"
follow_desktop_resolution="${UURB_FOLLOW_DESKTOP_RESOLUTION:-${saved_follow_desktop_resolution:-off}}"
bridge_display="${UURB_DISPLAY:-${saved_display:-auto}}"
desktop_target="${UURB_DESKTOP_TARGET:-${saved_desktop_target:-auto}}"
desktop_relay="${UURB_DESKTOP_RELAY:-${saved_desktop_relay:-rdp}}"
input_route="${UURB_INPUT_ROUTE:-${saved_input_route:-}}"
shared_vnc_port="${UURB_DESKTOP_VNC_PORT:-${saved_shared_vnc_port:-}}"
vnc_grab_keyboard="${UURB_VNC_GRAB_KEYBOARD:-${saved_vnc_grab_keyboard:-on}}"
grd_fd_restart_threshold="${UURB_GRD_FD_RESTART_THRESHOLD:-${saved_grd_fd_restart_threshold:-4096}}"
text_key_delay_ms="$(resolve_text_key_delay \
    "$environment_file" "$saved_text_key_delay_ms")"
physical_key_delay_ms="${UURB_PHYSICAL_KEY_DELAY_MS:-${saved_physical_key_delay_ms:-0}}"
keyboard_route="${UURB_KEYBOARD_ROUTE:-${saved_keyboard_route:-rdp}}"
phone_text_mode="${UURB_PHONE_TEXT_MODE:-${saved_phone_text_mode:-auto}}"
network_interface="${UURB_NETWORK_INTERFACE:-${saved_network_interface:-all}}"
cursor_guard="${UURB_CURSOR_GUARD:-${saved_cursor_guard:-}}"
cursor_size="${UURB_CURSOR_SIZE:-${saved_cursor_size:-auto}}"
console_vnc_port="${UURB_CONSOLE_VNC_PORT:-${saved_console_vnc_port:-5920}}"
console_web_port="${UURB_CONSOLE_WEB_PORT:-${saved_console_web_port:-6080}}"
window_scale="${UURB_WINDOW_SCALE:-${saved_window_scale:-1}}"
terminal_session_mode="${UURB_TERMINAL_SESSION_MODE:-${saved_terminal_session_mode:-fresh}}"
if [[ "$terminal_session_mode" != fresh && "$terminal_session_mode" != persistent ]]; then
    printf 'UURB_TERMINAL_SESSION_MODE must be fresh or persistent.\n' >&2
    exit 2
fi
if [[ ! "$window_scale" =~ ^(1|1\.5|2|3)$ ]]; then
    printf 'UU window scale must be 1, 1.5, 2, or 3.\n' >&2
    exit 2
fi
uu_installer=''
skip_packages=false
skip_account_login=false
start_service=true
fresh_install=false
unattended=false
automatic_updates=false
upgrade_existing=false
prefix_only=false

usage() {
    cat <<'EOF'
usage: ./install.sh [options]

  --uu-installer PATH    use a previously downloaded audited installer
  --release-manifest PATH
                         use an approved release manifest
  --rdp-port PORT        local GNOME RDP relay port (default: 3390)
  --resolution WxH       relay resolution (default: 1920x1080)
  --follow-desktop-resolution off|on
                         after a stable X11 desktop resize, align and restart
                         only the UU bridge (default: off)
  --display auto|:N      private X display (default: first free from :20)
  --desktop-target TARGET
                         shared GNOME desktop: auto, xrdp, physical, or :N
                         (default: auto; explicit targets never fall back)
  --desktop-relay rdp|vnc
                         private-canvas relay; use vnc only for an X11/XRDP
                         desktop when nested GNOME RDP renders blank
  --vnc-grab-keyboard off|on
                         grab the dedicated private VNC relay keyboard so
                         Shift/Ctrl survive nested input (default: on)
  --grd-fd-restart-threshold N
                         restart before GNOME RDP exhausts descriptors
                         (default: 4096; 0 disables the guard)
  --text-key-delay-ms N  pace relayed phone text by 0-50 ms per character
                         (new install: 8; v0.1.0 upgrade: preserve 0)
  --physical-key-delay-ms N
                         pace physical key batches by 0-50 ms
                         (default: 0)
  --keyboard-route rdp|x11|auto
                         route physical keys through the compatible RDP path,
                         directly into an X11 desktop, or auto-detect X11
                         (default: rdp; x11 is opt-in)
  --phone-text-mode auto|keys|clipboard
                         keep representable phone text on fast key events and
                         paste newline/CJK as semantic clipboard text (auto),
                         or force one route (default: auto)
  --network-interface all|default|IFACE
                         use all adapters (the default), Ubuntu's preferred
                         route, or one named interface
  --cursor-guard off|on  process-local UU cursor workaround
                         (new Debian install: on; Ubuntu: off)
  --cursor-size auto|N   with the cursor guard on, match the desktop cursor or
                         use a fixed size from 24 through 128 pixels
  --console-vnc-port N   localhost VNC sidecar port (default: 5920)
  --console-web-port N   localhost noVNC app port (default: 6080)
  --skip-packages        do not install distribution/Wine package dependencies
  --skip-account-login   do not open UU for first-time account sign-in
  --unattended           enable TPM-backed startup after an automatic login
  --automatic-updates    enable daily checks and resumable Codex repair
  --upgrade-existing     run an audited installer over the existing UU prefix
                         before applying its approved release manifest
  --prefix-only          prepare only the selected Wine prefix; do not change
                         RDP, user services, launchers, or account login
  --no-start             install and verify files without starting the service
  -h, --help             show this help
EOF
}

while (($#)); do
    case "$1" in
        --uu-installer)
            uu_installer="${2:?--uu-installer requires a path}"
            shift 2
            ;;
        --release-manifest)
            release_manifest="${2:?--release-manifest requires a path}"
            shift 2
            ;;
        --rdp-port)
            rdp_port="${2:?--rdp-port requires a port}"
            shift 2
            ;;
        --resolution)
            resolution="${2:?--resolution requires WIDTHxHEIGHT}"
            shift 2
            ;;
        --follow-desktop-resolution)
            follow_desktop_resolution="${2:?--follow-desktop-resolution requires off or on}"
            shift 2
            ;;
        --display)
            bridge_display="${2:?--display requires auto or :N}"
            shift 2
            ;;
        --desktop-target)
            desktop_target="${2:?--desktop-target requires auto, xrdp, physical, or :N}"
            shift 2
            ;;
        --desktop-relay)
            desktop_relay="${2:?--desktop-relay requires rdp or vnc}"
            shift 2
            ;;
        --vnc-grab-keyboard)
            vnc_grab_keyboard="${2:?--vnc-grab-keyboard requires off or on}"
            shift 2
            ;;
        --grd-fd-restart-threshold)
            grd_fd_restart_threshold="${2:?--grd-fd-restart-threshold requires a number}"
            shift 2
            ;;
        --text-key-delay-ms)
            text_key_delay_ms="${2:?--text-key-delay-ms requires a number}"
            shift 2
            ;;
        --physical-key-delay-ms)
            physical_key_delay_ms="${2:?--physical-key-delay-ms requires a number}"
            shift 2
            ;;
        --keyboard-route)
            keyboard_route="${2:?--keyboard-route requires rdp, x11, or auto}"
            shift 2
            ;;
        --phone-text-mode)
            phone_text_mode="${2:?--phone-text-mode requires auto, keys, or clipboard}"
            shift 2
            ;;
        --network-interface)
            network_interface="${2:?--network-interface requires all, default, or an interface name}"
            shift 2
            ;;
        --cursor-guard)
            cursor_guard="${2:?--cursor-guard requires off or on}"
            shift 2
            ;;
        --cursor-size)
            cursor_size="${2:?--cursor-size requires auto or a pixel size}"
            shift 2
            ;;
        --console-vnc-port)
            console_vnc_port="${2:?--console-vnc-port requires a port}"
            shift 2
            ;;
        --console-web-port)
            console_web_port="${2:?--console-web-port requires a port}"
            shift 2
            ;;
        --skip-packages)
            skip_packages=true
            shift
            ;;
        --skip-account-login)
            skip_account_login=true
            shift
            ;;
        --unattended)
            unattended=true
            shift
            ;;
        --automatic-updates)
            automatic_updates=true
            shift
            ;;
        --upgrade-existing)
            upgrade_existing=true
            shift
            ;;
        --prefix-only)
            prefix_only=true
            start_service=false
            skip_account_login=true
            shift
            ;;
        --no-start)
            start_service=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ $EUID -eq 0 ]]; then
    printf 'Run this installer as the desktop user, not as root.\n' >&2
    exit 1
fi
if [[ "$(uname -m)" != x86_64 ]]; then
    printf 'Only x86_64 Ubuntu or Debian 13 is currently supported.\n' >&2
    exit 1
fi
if [[ ! -r /etc/os-release ]]; then
    printf 'Cannot identify this operating system.\n' >&2
    exit 1
fi
# shellcheck source=/dev/null
source /etc/os-release
case "${ID:-}" in
    ubuntu)
        supported_ubuntu_releases='24.04 26.04'
        if [[ " $supported_ubuntu_releases " != *" ${VERSION_ID:-} "* ]] &&
           [[ "${UURB_ALLOW_UNVALIDATED_UBUNTU:-0}" != 1 ]]; then
            printf 'The validated Ubuntu hosts are %s; detected %s.\n' \
                "$supported_ubuntu_releases" "${VERSION_ID:-unknown}" >&2
            printf 'Set UURB_ALLOW_UNVALIDATED_UBUNTU=1 to attempt an unvalidated release.\n' >&2
            exit 1
        fi
        ;;
    debian)
        if [[ "${VERSION_ID:-}" != 13 || "${VERSION_CODENAME:-}" != trixie ]]; then
            printf 'The Debian port targets Debian 13 (trixie); detected %s %s.\n' \
                "${VERSION_ID:-unknown}" "${VERSION_CODENAME:-unknown}" >&2
            exit 1
        fi
        ;;
    *)
        printf 'Supported hosts are Ubuntu 24.04/26.04 and Debian 13; detected %s %s.\n' \
            "${ID:-unknown}" "${VERSION_ID:-unknown}" >&2
        exit 1
        ;;
esac
if [[ ! "${VERSION_CODENAME:-}" =~ ^[a-z][a-z0-9-]*$ ]]; then
    printf 'A valid distribution codename is required for the WineHQ source.\n' >&2
    exit 1
fi
winehq_sources_url="https://dl.winehq.org/wine-builds/$ID/dists/$VERSION_CODENAME/winehq-$VERSION_CODENAME.sources"
if [[ ! "$rdp_port" =~ ^[1-9][0-9]{0,4}$ ]] ||
   ((rdp_port < 1024 || rdp_port > 65535)); then
    printf 'The RDP port must be an integer from 1024 through 65535.\n' >&2
    exit 2
fi
if [[ ! "$resolution" =~ ^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$ ]]; then
    printf 'The resolution must use WIDTHxHEIGHT, for example 1920x1080.\n' >&2
    exit 2
fi
resolution_width="${resolution%x*}"
resolution_height="${resolution#*x}"
if ((resolution_width < 640 || resolution_width > 16384 ||
     resolution_height < 480 || resolution_height > 16384)); then
    printf 'The resolution must be between 640x480 and 16384x16384.\n' >&2
    exit 2
fi
if [[ "$follow_desktop_resolution" != off &&
      "$follow_desktop_resolution" != on ]]; then
    printf 'Desktop-resolution following must be off or on.\n' >&2
    exit 2
fi
if [[ "$bridge_display" != auto &&
      ! "$bridge_display" =~ ^:(0|[1-9][0-9]{0,2})$ ]]; then
    printf 'The private display must be auto or an X display such as :20.\n' >&2
    exit 2
fi
if [[ "$desktop_target" != auto &&
      "$desktop_target" != xrdp &&
      "$desktop_target" != physical &&
      ! "$desktop_target" =~ ^:(0|[1-9][0-9]{0,2})(\.0)?$ ]]; then
    printf 'The desktop target must be auto, xrdp, physical, or an X display such as :11.\n' >&2
    exit 2
fi
if [[ "$desktop_relay" != rdp && "$desktop_relay" != vnc ]]; then
    printf 'The desktop relay must be rdp or vnc.\n' >&2
    exit 2
fi
if [[ -z "$input_route" ]]; then
    # Debian 13's GNOME 48 does not advertise the relative-pointer capability
    # required by rdp-public. Keep the existing compatible SDL/RDP route.
    if [[ "$desktop_relay" == rdp && "$ID" != debian ]]; then
        input_route=rdp-public
    else
        input_route=legacy
    fi
fi
if [[ -z "${cursor_guard:-}" ]]; then
    cursor_guard=off
    if [[ "$ID" == debian ]]; then
        cursor_guard=on
    fi
fi
if [[ "$input_route" != legacy && "$input_route" != rdp-public ]] ||
   [[ "$input_route" == rdp-public && "$desktop_relay" != rdp ]]; then
    printf 'The input route must be legacy, or rdp-public with the RDP relay.\n' >&2
    exit 2
fi
if [[ -n "$shared_vnc_port" ]] && {
    [[ ! "$shared_vnc_port" =~ ^[1-9][0-9]{3}$ ]] ||
    ((shared_vnc_port < 5900 || shared_vnc_port > 5999));
}; then
    printf 'UURB_DESKTOP_VNC_PORT must be empty or in 5900..5999.\n' >&2
    exit 2
fi
if [[ "$vnc_grab_keyboard" != off && "$vnc_grab_keyboard" != on ]]; then
    printf 'VNC keyboard grabbing must be off or on.\n' >&2
    exit 2
fi
if [[ ! "$grd_fd_restart_threshold" =~ ^[0-9]+$ ]] ||
   ((grd_fd_restart_threshold != 0 &&
     (grd_fd_restart_threshold < 512 ||
      grd_fd_restart_threshold > 60000))); then
    printf 'The GNOME RDP descriptor threshold must be 0 or 512 through 60000.\n' >&2
    exit 2
fi
if [[ ! "$text_key_delay_ms" =~ ^[0-9]+$ ]] ||
   ((text_key_delay_ms > 50)); then
    printf 'The text-key delay must be an integer from 0 through 50 ms.\n' >&2
    exit 2
fi
if [[ ! "$physical_key_delay_ms" =~ ^[0-9]+$ ]] ||
   ((physical_key_delay_ms > 50)); then
    printf 'The physical-key delay must be an integer from 0 through 50 ms.\n' >&2
    exit 2
fi
if [[ "$keyboard_route" != rdp && "$keyboard_route" != x11 &&
      "$keyboard_route" != auto ]]; then
    printf 'The keyboard route must be rdp, x11, or auto.\n' >&2
    exit 2
fi
if [[ "$phone_text_mode" != auto && "$phone_text_mode" != keys &&
      "$phone_text_mode" != clipboard ]]; then
    printf 'The phone-text mode must be auto, keys, or clipboard.\n' >&2
    exit 2
fi
if [[ "$network_interface" != all &&
      "$network_interface" != default &&
      ! "$network_interface" =~ ^[a-zA-Z0-9_.:-]{1,15}$ ]]; then
    printf 'The UU network interface must be all, default, or a Linux interface name.\n' >&2
    exit 2
fi
if [[ "$network_interface" != all &&
      "$network_interface" != default &&
      ! -d "/sys/class/net/$network_interface" ]]; then
    printf 'The requested UU network interface does not exist: %s\n' \
        "$network_interface" >&2
    exit 2
fi
if [[ "$cursor_guard" != off && "$cursor_guard" != on ]]; then
    printf 'The cursor guard must be off or on.\n' >&2
    exit 2
fi
if [[ "$cursor_size" != auto ]]; then
    if [[ ! "$cursor_size" =~ ^[0-9]{1,3}$ ]]; then
        printf 'The cursor size must be auto or an integer from 24 through 128.\n' >&2
        exit 2
    fi
    cursor_size=$((10#$cursor_size))
    if ((cursor_size < 24 || cursor_size > 128)); then
        printf 'The cursor size must be auto or an integer from 24 through 128.\n' >&2
        exit 2
    fi
fi
if [[ ! "$console_vnc_port" =~ ^[1-9][0-9]{3,4}$ ||
      ! "$console_web_port" =~ ^[1-9][0-9]{3,4}$ ]]; then
    printf 'The UU console ports must be integers from 1024 through 65535.\n' >&2
    exit 2
fi
console_vnc_port=$((10#$console_vnc_port))
console_web_port=$((10#$console_web_port))
if ((console_vnc_port < 1024 || console_vnc_port > 65535 ||
     console_web_port < 1024 || console_web_port > 65535)) ||
   ((console_vnc_port == console_web_port)); then
    printf 'The UU console ports must be distinct values from 1024 through 65535.\n' \
        >&2
    exit 2
fi
if [[ "$upgrade_existing" == true && -z "$uu_installer" ]]; then
    printf -- '--upgrade-existing requires --uu-installer with an audited file.\n' >&2
    exit 2
fi
if [[ "$upgrade_existing" == true &&
      ! -f "$uu_dir/GameViewer.exe" ]]; then
    printf -- '--upgrade-existing requires an existing UU installation in %s.\n' \
        "$wine_prefix" >&2
    exit 2
fi
if [[ "$prefix_only" == true &&
      ("$unattended" == true || "$automatic_updates" == true) ]]; then
    printf -- '--prefix-only cannot configure unattended or automatic updates.\n' >&2
    exit 2
fi
if [[ "$prefix_only" == false ]]; then
    user_bus="${XDG_RUNTIME_DIR:-/run/user/$UID}/bus"
    if [[ ! -S "$user_bus" ]]; then
        printf 'The systemd user bus is unavailable at %s.\n' "$user_bus" >&2
        printf 'Log into the target GNOME desktop as this user, then rerun.\n' >&2
        exit 1
    fi
fi
if [[ "$unattended" == true && "$start_service" == false ]]; then
    printf -- '--unattended cannot be combined with --no-start.\n' >&2
    exit 2
fi

install_winehq() {
    local codename
    local temporary

    if [[ -x "$wine_bin" ]]; then
        return
    fi
    codename="${VERSION_CODENAME:?Distribution codename is unavailable}"
    temporary="$(mktemp -d)"

    sudo dpkg --add-architecture i386
    curl -fsSL https://dl.winehq.org/wine-builds/winehq.key \
        -o "$temporary/winehq.key"
    curl -fsSL "$winehq_sources_url" \
        -o "$temporary/winehq-$codename.sources"
    sudo install -d -m 0755 /etc/apt/keyrings
    # apt >= 3 (Ubuntu 26.04) rejects ASCII-armored keys; dearmor to binary.
    gpg --batch --yes --dearmor -o "$temporary/winehq-archive.key.gpg" \
        "$temporary/winehq.key"
    sudo install -m 0644 "$temporary/winehq-archive.key.gpg" \
        /etc/apt/keyrings/winehq-archive.key
    sudo install -m 0644 "$temporary/winehq-$codename.sources" \
        "/etc/apt/sources.list.d/winehq-$codename.sources"
    sudo apt-get update
    sudo apt-get install -y --install-recommends winehq-stable
    rm -rf "$temporary"
}

install_packages() {
    local -a distribution_packages=()
    if [[ "$ID" == debian ]]; then
        # The exact Ubuntu reference tools live in a container, never in /usr.
        distribution_packages+=(build-essential)
        if [[ -z "${UURB_FREERDP_PREBUILT_DIR:-}" ]] &&
           ! "$python_bin" "$repo_dir/scripts/verify-freerdp-runtime.py" \
                "$freerdp_build" >/dev/null 2>&1; then
            # A Docker CLI may target an unavailable, rootless or remapped daemon.
            # Provision the supported rootless fallback for every cold build.
            distribution_packages+=(podman uidmap)
        fi
    fi
    sudo apt-get update
    sudo apt-get install -y \
        acl aria2 binutils ca-certificates cmake crudini curl \
        gcc \
        gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 nasm \
        fonts-noto-cjk gir1.2-gtk-3.0 git gnome-remote-desktop gnupg iproute2 jq libsecret-tools libx11-6 \
        libxml2-utils libxtst6 meson novnc \
        ninja-build openbox openssl p7zip-full patch python3 python3-attr \
        python3-gi python3-jinja2 python3-xlib python3-pil tar tigervnc-viewer websockify \
        x11-utils x11-xserver-utils x11vnc xauth xclip xcompmgr zenity \
        xdotool xvfb zstd "${distribution_packages[@]}"
    install_winehq
    if [[ "$ID" == debian ]]; then
        # WineHQ's runtime includes winegcc, but Winelib headers/import libraries
        # are in a separate package, including when Wine was installed earlier.
        sudo apt-get install -y --install-recommends wine-stable-dev
    fi
}

stop_wine_prefix() {
    "$repo_dir/scripts/stop-wine-prefix" "$wine_prefix" "$wineserver_bin"
}

download_verified() {
    local url="$1"
    local expected="$2"
    local destination="$3"
    local attempt

    if [[ -f "$destination" ]] &&
       printf '%s  %s\n' "$expected" "$destination" | sha256sum -c - \
           >/dev/null 2>&1; then
        return
    fi

    mkdir -p "$(dirname -- "$destination")"
    for attempt in 1 2; do
        if command -v aria2c >/dev/null 2>&1; then
            aria2c --allow-overwrite=true --auto-file-renaming=false \
                --continue=true --max-connection-per-server=8 \
                --max-tries=5 --min-split-size=1M --retry-wait=2 --split=8 \
                --dir="$(dirname -- "$destination")" \
                --out="$(basename -- "$destination").part" "$url"
        else
            curl --continue-at - --fail --location --retry 3 \
                --output "$destination.part" "$url"
        fi
        if printf '%s  %s\n' "$expected" "$destination.part" | \
            sha256sum -c -; then
            mv "$destination.part" "$destination"
            rm -f "$destination.part.aria2"
            return
        fi
        rm -f "$destination.part" "$destination.part.aria2"
        printf 'download hash mismatch; retrying %s (%s/2)\n' \
            "$url" "$attempt" >&2
    done

    printf 'download verification failed: %s\n' "$url" >&2
    exit 1
}

if [[ "$skip_packages" == false ]]; then
    install_packages
fi

for command in curl meson ninja patch readelf sha256sum /usr/bin/systemctl \
    timeout \
    "$grdctl_bin" "$openssl_bin" "$python_bin" "$secret_tool_bin" \
    "$wine_bin" "$wineserver_bin" /usr/bin/Xvfb /usr/bin/gsettings \
    /usr/bin/awk /usr/bin/ip /usr/bin/mcookie /usr/bin/openbox \
    /usr/bin/script /usr/bin/sort /usr/bin/ss /usr/bin/xauth \
    /usr/bin/vncviewer /usr/bin/websockify /usr/bin/x11vnc /usr/bin/xclip \
    /usr/bin/xcompmgr /usr/bin/xdotool \
    /usr/libexec/gnome-remote-desktop-daemon; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'missing required command: %s\n' "$command" >&2
        exit 1
    fi
done

release_manifest="$(realpath "$release_manifest")"
manifest_field() {
    "$python_bin" "$repo_dir/scripts/patch-gameviewer.py" field "$1" \
        --manifest "$release_manifest"
}

uu_download_url="$(manifest_field installer.url)"
uu_installer_filename="$(manifest_field installer.filename)"
uu_installer_sha256="$(manifest_field installer.sha256)"
release_version="$(manifest_field version)"
server_exe="$uu_bin/$(manifest_field server.filename)"
healthd_exe="$uu_bin/$(manifest_field health_monitor.filename)"
healthd_sha256="$(manifest_field health_monitor.original_sha256)"
devcon_exe="$uu_bin/drivers/devcon.exe"
devcon_backup="$devcon_exe.uu-original"
case "$release_version" in
    4.33.0.8907|4.34.0.8979|4.39.1.1375|4.39.2.1561|4.42.0.2770)
        devcon_sha256='46731d6ea59dd9b63ad641c79646bb5ff64e1b877a1226536e3fe34d1ab4ee10'
        ;;
    *)
        printf 'No audited devcon.exe identity exists for UU %s.\n' \
            "$release_version" >&2
        exit 1
        ;;
esac

export WINEPREFIX="$wine_prefix"
export WINEDEBUG=-all
export WINEDLLOVERRIDES='winedbg.exe=d;mscoree,mshtml='

memory_total_kib="$(/usr/bin/awk '$1 == "MemTotal:" { print $2; exit }' /proc/meminfo)"
manage_memory_budget() {
"$python_bin" - "$HOME/.config/systemd/user" "$memory_total_kib" "$1" <<'PY_MEMORY_BUDGET'
import os
import re
import sys
import tempfile
from pathlib import Path

unit_directory = Path(sys.argv[1])
total_text = sys.argv[2]
mode = sys.argv[3]
if mode not in ("preflight", "apply"):
    raise SystemExit("Invalid memory budget phase.")
if not total_text.isdecimal() or int(total_text) <= 0:
    raise SystemExit("Could not read host RAM; memory protection was preserved.")
directory = unit_directory / "uu-remote-bridge.service.d"
destination = directory / "10-uu-remote-memory-budget.conf"
content = ("# Managed by UU Remote installer: host memory budget v1\n"
           "[Service]\nMemoryHigh=6G\nMemoryMax=8G\n")
if unit_directory.is_symlink() or directory.is_symlink() or destination.is_symlink():
    raise SystemExit(f"Refusing symbolic-link memory budget path: {destination}")
if any(path.exists() and not path.is_dir() for path in (unit_directory, directory)):
    raise SystemExit(f"Refusing non-directory memory budget path: {directory}")
if destination.exists() and (not destination.is_file() or destination.read_text() != content):
    raise SystemExit(f"Refusing to replace customized memory budget file: {destination}")
# Explicit user memory policy takes precedence regardless of its filename.
custom_policy = any(path != destination and path.is_file() and
                    re.search(r"^\s*(?:Startup)?Memory(?:High|Max)\s*=", path.read_text(), re.M)
                    for path in directory.glob("*.conf"))
if mode == "preflight":
    raise SystemExit(0)
if int(total_text) < 16 * 1024 * 1024 or custom_policy:
    destination.unlink(missing_ok=True)
    print("UU memory budget: preserving user policy or small-host 3G/4G baseline.")
else:
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".uu-memory-budget-", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(content)
            os.fchmod(stream.fileno(), 0o644)
        os.replace(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)
    print("UU memory budget: 6G high / 8G maximum (host RAM >= 16 GiB).")
PY_MEMORY_BUDGET
}

# Validate pinned SDK inputs, launcher links and budgets before stopping the bridge.
"$python_bin" "$repo_dir/scripts/verify-freerdp-input-sdk.py"
if ! "$python_bin" -c 'import Xlib; import Xlib.display; import Xlib.ext.composite; from PIL import Image; import gi; gi.require_version("Gtk", "3.0"); from gi.repository import Gtk'; then
    printf 'Missing clipboard runtime; install python3-xlib, python3-pil, python3-gi and gir1.2-gtk-3.0 before restarting UU.\n' >&2
    exit 1
fi
if [[ "$prefix_only" == false ]]; then
    manage_memory_budget preflight
    "$python_bin" - "$HOME" <<'PY_LAUNCHER_PREFLIGHT'
import sys
from pathlib import Path

home = Path(sys.argv[1])
applications = home / ".local/share/applications"
backups = home / ".local/share/uu-remote/tools/launcher-backups"
entries = [applications / (name + ".desktop") for name in
           ("uu-remote", "uu-quality", "uu-vnc-viewer", "xtigervncviewer", "x11vnc", "xfreerdp", "obconf")]
entries.append(home / "Desktop/UU Remote.desktop")
for path in [applications, home / "Desktop", backups.parent, backups, *entries,
             *(backups / entry.name for entry in entries)]:
    if path.is_symlink():
        raise SystemExit(f"Refusing symbolic-link desktop launcher path: {path}")
PY_LAUNCHER_PREFLIGHT
fi

# Complete cold builds and approved closure checks before interrupting UU.
system_libei_version="$(
    dpkg-query -W -f='${Version}' libei1 2>/dev/null || true)"
if [[ -n "$system_libei_version" ]] &&
   dpkg --compare-versions "$system_libei_version" ge 1.3; then
    detected_libei_mode=system
else
    detected_libei_mode=backport
fi
libei_mode="${UURB_LIBEI_MODE:-${saved_libei_mode:-$detected_libei_mode}}"
if [[ "$libei_mode" == auto ]]; then
    libei_mode="$detected_libei_mode"
fi
if [[ "$libei_mode" != system && "$libei_mode" != backport ]]; then
    printf 'The libei mode must be system or backport.\n' >&2
    exit 2
fi
printf 'libei mode: %s (system libei %s)\n' \
    "$libei_mode" "${system_libei_version:-absent}"

"$repo_dir/scripts/build-compat.sh" "$compat_build"
"$repo_dir/scripts/build-full-input-loader.sh" "$compat_build"
if [[ "$ID" == debian && -z "${UURB_FREERDP_PREBUILT_DIR:-}" ]] &&
   ! "$python_bin" "$repo_dir/scripts/verify-freerdp-runtime.py" \
        "$freerdp_build" >/dev/null 2>&1; then
    "$python_bin" "$repo_dir/scripts/build-reference-relay.py" \
        --output "$freerdp_build"
fi
"$repo_dir/scripts/build-winpr.sh" "$freerdp_build"
if [[ "$libei_mode" == backport ]]; then
    "$repo_dir/scripts/build-libei.sh" "$libei_build"
fi

"$python_bin" "$repo_dir/scripts/verify-freerdp-runtime.py" "$freerdp_build"

bridge_was_active=false
if [[ "$prefix_only" == false ]] &&
   "${systemctl_user[@]}" is-active --quiet uu-remote-bridge.service; then
    bridge_was_active=true
fi
restore_bridge_after_failure() {
    local status=$?

    if ((status != 0)) && [[ "$bridge_was_active" == true ]]; then
        "${systemctl_user[@]}" start uu-remote-bridge.service \
            >/dev/null 2>&1 || true
    fi
}
trap restore_bridge_after_failure EXIT

if [[ "$prefix_only" == false ]]; then
    port_listener="$(/usr/bin/ss -H -ltnp "sport = :$rdp_port" 2>/dev/null || true)"
    if [[ -n "$port_listener" ]] &&
       ! /usr/bin/grep -q 'gnome-remote-de' <<<"$port_listener"; then
        printf 'RDP port %s is already owned by another process:\n%s\n' \
            "$rdp_port" "$port_listener" >&2
        exit 1
    fi
    "${systemctl_user[@]}" stop uu-remote-bridge.service >/dev/null 2>&1 || true
fi
stop_wine_prefix

if [[ "$prefix_only" == false && "$bridge_display" != auto ]]; then
    display_number="${bridge_display#:}"
    if [[ -e "/tmp/.X11-unix/X$display_number" ||
          -e "/tmp/.X${display_number}-lock" ]]; then
        printf 'Private X display %s is already in use; use --display auto.\n' \
            "$bridge_display" >&2
        exit 1
    fi
fi

if [[ ! -f "$uu_dir/GameViewer.exe" || "$upgrade_existing" == true ]]; then
    if [[ ! -f "$uu_dir/GameViewer.exe" ]]; then
        fresh_install=true
    fi
    mkdir -p "$repo_dir/build/downloads"
    if [[ -z "$uu_installer" ]]; then
        uu_installer="$repo_dir/build/downloads/$uu_installer_filename"
        download_verified "$uu_download_url" "$uu_installer_sha256" \
            "$uu_installer"
    else
        uu_installer="$(realpath "$uu_installer")"
    fi
    printf '%s  %s\n' "$uu_installer_sha256" "$uu_installer" | \
        sha256sum -c -
    mkdir -p "$wine_prefix"
    if [[ "$fresh_install" == true ]]; then
        "$python_bin" "$repo_dir/scripts/manage-terminal-runtime.py" \
            detach-system-links "$wine_prefix"
        "$wine_bin" wineboot -u
        "$wine_bin" winecfg -v win10
        if [[ "$ID" == debian ]]; then
            # Apply before the official installer can launch its manager. This
            # setting is scoped to this application's new, dedicated prefix.
            "$wine_bin" reg add \
                'HKCU\Software\Wine\AppDefaults\GameViewer.exe\Direct3D' \
                /v renderer /t REG_SZ /d gdi /f
        fi
    else
        if [[ ! -f "$installed_manifest" ]]; then
            printf 'Cannot upgrade without the currently installed release manifest.\n' >&2
            exit 1
        fi
        previous_version="$(
            "$python_bin" "$repo_dir/scripts/patch-gameviewer.py" field version \
                --manifest "$installed_manifest"
        )"
        previous_server_filename="$(
            "$python_bin" "$repo_dir/scripts/patch-gameviewer.py" field \
                server.filename --manifest "$installed_manifest"
        )"
        previous_healthd_filename="$(
            "$python_bin" "$repo_dir/scripts/patch-gameviewer.py" field \
                health_monitor.filename --manifest "$installed_manifest"
        )"
        previous_backup_dir="$wine_prefix/compat/release-backups/$previous_version"
        mkdir -p "$previous_backup_dir"
        install -m 0600 "$installed_manifest" \
            "$previous_backup_dir/release-manifest.json"
        for previous_backup in \
            "$uu_bin/$previous_server_filename.uu-original" \
            "$uu_bin/$previous_healthd_filename.uu-original"; do
            if [[ -f "$previous_backup" ]]; then
                install -m 0600 "$previous_backup" \
                    "$previous_backup_dir/${previous_backup##*/}"
                rm -f "$previous_backup"
            fi
        done
    fi
    "$wine_bin" "$uu_installer" /S
    stop_wine_prefix
fi
if [[ ! -f "$server_exe" || ! -f "$healthd_exe" ]]; then
    printf 'UU Remote installation did not produce the expected files.\n' >&2
    exit 1
fi
"$python_bin" "$repo_dir/scripts/patch-gameviewer.py" verify "$server_exe" \
    --manifest "$release_manifest" >/dev/null


mkdir -p "$wine_prefix/compat" "$freerdp_install"
if [[ "$libei_mode" == backport ]]; then
    mkdir -p "$libei_install"
fi
if [[ -e "$terminal_proxy_install" ]] &&
   { [[ ! -f "$installed_terminal_proxy" ]] ||
     ! /usr/bin/cmp -s "$terminal_proxy_install" \
        "$installed_terminal_proxy"; }; then
    printf 'Refusing to replace an unknown GameViewer bin/powershell.exe.\n' >&2
    exit 1
fi
"$python_bin" "$repo_dir/scripts/manage-terminal-runtime.py" \
    install "$wine_prefix" --build "$compat_build"
install -m 0644 "$release_manifest" "$installed_manifest"
install -m 0755 \
    "$compat_build/uu-cursor-guard.dll" \
    "$compat_build/uu-input-bridge.dll" \
    "$compat_build/uu-input-broker.exe" \
    "$compat_build/uurb-full-input-client.dll" \
    "$compat_build/uu-input-broker-probe.exe" \
    "$compat_build/uu-injector.exe" \
    "$compat_build/uu-service-control.exe" \
    "$compat_build/uu-display-mode.exe" \
    "$compat_build/uu-wine-clipboard-bridge.exe" \
    "$compat_build/uu-terminal-proxy.exe" \
    "$wine_prefix/compat/"
install -m 0755 "$compat_build/uu-network-filter.so" \
    "$wine_prefix/compat/uu-network-filter.so"
install -m 0755 "$compat_build/uu-manager-capture.so" \
    "$wine_prefix/compat/uu-manager-capture.so"
install -m 0755 "$repo_dir/scripts/uu-controller-fit.py" \
    "$wine_prefix/compat/uu-controller-fit.py"
install -m 0755 "$repo_dir/scripts/uu-display-modes.py" \
    "$wine_prefix/compat/uu-display-modes.py"
install -m 0755 "$compat_build/uu-x11-input" \
    "$wine_prefix/compat/uu-x11-input"
install -m 0755 "$compat_build/uu-x11-clipboard" \
    "$wine_prefix/compat/uu-x11-clipboard"
install -m 0755 "$compat_build/uu-terminal-bridge" \
    "$wine_prefix/compat/uu-terminal-bridge"
install -m 0755 "$compat_build/uu-terminal-proxy.exe" \
    "$terminal_proxy_install"
install -m 0755 "$compat_build/winlogon.exe" \
    "$wine_prefix/compat/winlogon.exe"
install -m 0755 "$compat_build/winlogon.exe.so" \
    "$wine_prefix/compat/winlogon.exe.so"
while IFS= read -r freerdp_file; do
    mkdir -p "$freerdp_install/$(dirname -- "$freerdp_file")"
    install -m 0755 "$freerdp_build/$freerdp_file" "$freerdp_install/$freerdp_file"
done < <("$python_bin" "$repo_dir/scripts/verify-freerdp-runtime.py" --mode list)
install -m 0755 "$compat_build/uurb-full-input-client.dll" \
    "$freerdp_install/uurb-full-input-client.dll"
install -m 0755 "$compat_build/libuurb-full-input-client.dll" \
    "$freerdp_install/libuurb-full-input-client.dll"
install -m 0755 "$compat_build/uu-input-bridge-public.dll" \
    "$freerdp_install/uu-input-bridge.dll"
if [[ "$libei_mode" == backport ]]; then
    install -m 0755 "$libei_build/libei.so.1.2.1" \
        "$libei_install/libei.so.1.2.1"
    ln -sfn libei.so.1.2.1 "$libei_install/libei.so.1"
fi
for freerdp_receipt in .build-recipe .build-sha256 .source-provenance.json; do
    install -m 0644 "$freerdp_build/$freerdp_receipt" "$freerdp_install/$freerdp_receipt"
done
"$python_bin" "$repo_dir/scripts/verify-freerdp-runtime.py" "$freerdp_install"
runtime_digest_tmp="$(mktemp "$wine_prefix/compat/.runtime-source-sha256.XXXXXX")"
"$repo_dir/scripts/runtime-source-digest" >"$runtime_digest_tmp"
chmod 0644 "$runtime_digest_tmp"
mv "$runtime_digest_tmp" "$runtime_digest_file"

healthd_backup="$healthd_exe.uu-original"
healthd_current_hash="$(sha256sum "$healthd_exe" | awk '{print $1}')"
if [[ "$healthd_current_hash" == "$healthd_sha256" ]]; then
    [[ -f "$healthd_backup" ]] || cp -p "$healthd_exe" "$healthd_backup"
elif [[ ! -f "$healthd_backup" ]] || \
     [[ "$(sha256sum "$healthd_backup" | awk '{print $1}')" != "$healthd_sha256" ]]; then
    printf 'Refusing to replace an unknown GameViewerHealthd.exe build.\n' >&2
    exit 1
fi
install -m 0755 "$compat_build/uu-healthd-stub.exe" "$healthd_exe"

if [[ -f "$devcon_exe" ]]; then
    if [[ "$(sha256sum "$devcon_exe" | awk '{print $1}')" != \
          "$devcon_sha256" ]]; then
        printf 'Refusing to suppress an unknown devcon.exe build.\n' >&2
        exit 1
    fi
    if [[ -f "$devcon_backup" ]]; then
        if [[ "$(sha256sum "$devcon_backup" | awk '{print $1}')" != \
              "$devcon_sha256" ]]; then
            printf 'Refusing to use an unknown devcon.exe backup.\n' >&2
            exit 1
        fi
        rm -f "$devcon_exe"
    else
        mv "$devcon_exe" "$devcon_backup"
    fi
elif [[ ! -f "$devcon_backup" ]] || \
     [[ "$(sha256sum "$devcon_backup" | awk '{print $1}')" != \
        "$devcon_sha256" ]]; then
    printf 'The suppressed devcon.exe has no audited backup.\n' >&2
    exit 1
fi

"$python_bin" "$repo_dir/scripts/patch-gameviewer.py" patch "$server_exe" \
    --manifest "$installed_manifest"
"$repo_dir/scripts/clean-wine-device-registry" "$wine_prefix"

if [[ "$prefix_only" == true ]]; then
    printf '\nPrepared approved UU release in %s without changing RDP configuration or opening the login UI.\n' \
        "$wine_prefix"
    exit 0
fi

install -d -m 0755 \
    "$HOME/.local/bin" "$HOME/.local/libexec" \
    "$HOME/.config/systemd/user" "$HOME/.local/share/applications"
install -d -m 0700 "$config_dir"
environment_tmp="$(mktemp "$config_dir/.environment.XXXXXX")"
printf 'UURB_RDP_PORT=%s\n' "$rdp_port" >"$environment_tmp"
printf 'UURB_WINEPREFIX=%s\n' "$wine_prefix" >>"$environment_tmp"
printf 'UURB_RESOLUTION=%s\n' "$resolution" >>"$environment_tmp"
printf 'UURB_FOLLOW_DESKTOP_RESOLUTION=%s\n' \
    "$follow_desktop_resolution" >>"$environment_tmp"
printf 'UURB_DISPLAY=%s\n' "$bridge_display" >>"$environment_tmp"
printf 'UURB_DESKTOP_TARGET=%s\n' "$desktop_target" >>"$environment_tmp"
printf 'UURB_DESKTOP_RELAY=%s\n' "$desktop_relay" >>"$environment_tmp"
printf 'UURB_INPUT_ROUTE=%s\n' "$input_route" >>"$environment_tmp"
if [[ -n "$shared_vnc_port" ]]; then
    printf 'UURB_DESKTOP_VNC_PORT=%s\n' "$shared_vnc_port" >>"$environment_tmp"
fi
printf 'UURB_VNC_GRAB_KEYBOARD=%s\n' \
    "$vnc_grab_keyboard" >>"$environment_tmp"
printf 'UURB_GRD_FD_RESTART_THRESHOLD=%s\n' \
    "$grd_fd_restart_threshold" >>"$environment_tmp"
printf 'UURB_TEXT_KEY_DELAY_MS=%s\n' \
    "$text_key_delay_ms" >>"$environment_tmp"
printf 'UURB_PHYSICAL_KEY_DELAY_MS=%s\n' \
    "$physical_key_delay_ms" >>"$environment_tmp"
printf 'UURB_KEYBOARD_ROUTE=%s\n' \
    "$keyboard_route" >>"$environment_tmp"
printf 'UURB_PHONE_TEXT_MODE=%s\n' \
    "$phone_text_mode" >>"$environment_tmp"
printf 'UURB_NETWORK_INTERFACE=%s\n' \
    "$network_interface" >>"$environment_tmp"
printf 'UURB_CURSOR_GUARD=%s\n' \
    "$cursor_guard" >>"$environment_tmp"
printf 'UURB_CURSOR_SIZE=%s\n' \
    "$cursor_size" >>"$environment_tmp"
printf 'UURB_CONSOLE_VNC_PORT=%s\n' \
    "$console_vnc_port" >>"$environment_tmp"
printf 'UURB_CONSOLE_WEB_PORT=%s\n' \
    "$console_web_port" >>"$environment_tmp"
printf 'UURB_WINDOW_SCALE=%s\n' "$window_scale" >>"$environment_tmp"
printf 'UURB_LIBEI_MODE=%s\n' "$libei_mode" >>"$environment_tmp"
printf 'UURB_TERMINAL_SESSION_MODE=%s\n' "$terminal_session_mode" >>"$environment_tmp"
chmod 0600 "$environment_tmp"
mv "$environment_tmp" "$environment_file"
install -m 0755 "$repo_dir/scripts/uu-remote-bridge" \
    "$HOME/.local/bin/uu-remote-bridge"
install -m 0755 "$repo_dir/scripts/uu-shared-physical-vnc" \
    "$HOME/.local/bin/uu-shared-physical-vnc"
install -m 0755 "$repo_dir/scripts/uu-remote" "$HOME/.local/bin/uu-remote"
install -m 0755 "$repo_dir/scripts/uu-remote-console" \
    "$HOME/.local/bin/uu-remote-console"
install -m 0755 "$repo_dir/scripts/uu-agent" "$HOME/.local/bin/uu-agent"
install -m 0755 "$repo_dir/scripts/uu-ssh" "$HOME/.local/bin/uu-ssh"
install -m 0755 "$repo_dir/scripts/uu-shell" "$HOME/.local/bin/uu-shell"
install -m 0755 "$repo_dir/scripts/uu-link" "$HOME/.local/bin/uu-link"
install -m 0755 "$repo_dir/scripts/upgrade-uu-remote.sh" \
    "$HOME/.local/bin/uu-remote-upgrade"
install -m 0755 "$repo_dir/scripts/stop-wine-prefix" \
    "$HOME/.local/libexec/uu-remote-stop-wine-prefix"
install -m 0755 "$repo_dir/scripts/clean-wine-device-registry" \
    "$HOME/.local/libexec/uu-clean-wine-device-registry"
install -m 0755 "$repo_dir/scripts/inspect-wine-device-registry.py" \
    "$HOME/.local/libexec/uu-inspect-wine-device-registry.py"
install -m 0755 "$repo_dir/scripts/uu_connection_status.py" \
    "$HOME/.local/libexec/uu-connection-status"
install -m 0755 "$repo_dir/scripts/uu-cursor-asset.py" \
    "$HOME/.local/libexec/uu-cursor-asset.py"
install -m 0755 "$repo_dir/scripts/uu-quality.py" \
    "$HOME/.local/libexec/uu-quality.py"
install -m 0755 "$repo_dir/scripts/uu-clipboard-native.py" \
    "$HOME/.local/libexec/uu-clipboard-native.py"
install -D -m 0644 "$repo_dir/VERSION" "$HOME/.local/share/uu-remote/tools/VERSION"
install -m 0755 "$repo_dir/scripts/uu-desktop-tool.py" \
    "$HOME/.local/libexec/uu-desktop-tool.py"
install -D -m 0644 "$repo_dir/desktop/uu-tools-fonts.conf" \
    "$HOME/.local/share/uu-remote/tools/uu-tools-fonts.conf"
install -m 0755 "$repo_dir/scripts/uu-full-ready.py" \
    "$HOME/.local/share/uu-remote/tools/uu-full-ready.py"
install -m 0755 "$repo_dir/scripts/uu-manual-plane.py" \
    "$HOME/.local/share/uu-remote/tools/uu-manual-plane.py"
install -m 0755 "$repo_dir/scripts/uu-keyring-unlock.py" \
    "$HOME/.local/bin/uu-keyring-unlock"
install -m 0644 "$repo_dir/systemd/uu-remote-bridge.service" \
    "$HOME/.config/systemd/user/uu-remote-bridge.service"
manage_memory_budget apply
install -m 0644 "$repo_dir/systemd/uu-shared-physical-vnc.service" \
    "$HOME/.config/systemd/user/uu-shared-physical-vnc.service"
install -m 0644 "$repo_dir/systemd/uu-remote-console.service" \
    "$HOME/.config/systemd/user/uu-remote-console.service"
install -m 0644 "$repo_dir/systemd/uu-keyring-unlock.service" \
    "$HOME/.config/systemd/user/uu-keyring-unlock.service"

desktop_entry="$HOME/.local/share/applications/uu-remote.desktop"
"$python_bin" - "$repo_dir/desktop/uu-remote.desktop.in" \
    "$desktop_entry" "$HOME/.local/bin/uu-remote" "$HOME" <<'PY_MAIN_LAUNCHER'
import os
import shutil
import sys
import tempfile
from pathlib import Path

template, destination, executable, home = map(Path, sys.argv[1:])
escaped = str(executable).replace("\\", "\\\\").replace(" ", "\\ ")
rendered = template.read_text(encoding="utf-8").replace(
    "@EXEC@", f"{escaped} open"
).replace(
    "@EXEC_QUALITY@", f"{escaped} quality gui"
) + "X-UURB-Managed=true\n"
quality = template.with_name("uu-quality.desktop.in").read_text(encoding="utf-8").replace(
    "@EXEC_QUALITY@", f"{escaped} quality gui"
) + "X-UURB-Managed=true\n"
entries = [(destination, rendered)]
if (home / "Desktop").is_dir():
    entries.append((home / "Desktop/UU Remote.desktop", rendered))
entries.append((destination.with_name("uu-quality.desktop"), quality))
for entry, content in entries:
    if entry.is_symlink():
        raise SystemExit(f"Refusing symbolic-link desktop launcher: {entry}")
    backups = home / ".local/share/uu-remote/tools/launcher-backups"
    if backups.is_symlink() or backups.parent.is_symlink():
        raise SystemExit(f"Refusing symbolic-link launcher backup directory: {backups}")
    backup = backups / entry.name
    if backup.is_symlink():
        raise SystemExit(f"Refusing symbolic-link launcher backup: {backup}")
    if entry.exists() and "X-UURB-Managed=true" not in entry.read_text(encoding="utf-8").splitlines():
        backups.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(entry, backup)
    fd, temporary = tempfile.mkstemp(prefix=".uu-launcher-", dir=entry.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(content)
            os.fchmod(stream.fileno(), 0o755 if entry == home / "Desktop/UU Remote.desktop" else 0o644)
        os.replace(temporary, entry)
    finally:
        Path(temporary).unlink(missing_ok=True)
PY_MAIN_LAUNCHER
"$python_bin" - "$repo_dir/desktop" "$HOME" <<'PY_TOOL_LAUNCHERS'
import os
import shutil
import sys
import tempfile
from pathlib import Path

templates, home = map(Path, sys.argv[1:])
applications = home / ".local/share/applications"
backups = home / ".local/share/uu-remote/tools/launcher-backups"
if backups.is_symlink() or backups.parent.is_symlink():
    raise SystemExit(f"Refusing symbolic-link launcher backup directory: {backups}")
for name, executable in (("uu-vnc-viewer", "/usr/bin/xtigervncviewer"),
                         ("xtigervncviewer", "/usr/bin/xtigervncviewer"),
                         ("x11vnc", "/usr/bin/x11vnc"),
                         ("xfreerdp", "/usr/bin/xfreerdp"),
                         ("obconf", "/usr/bin/obconf")):
    if not Path(executable).is_file():
        continue
    destination = applications / (name + ".desktop")
    backup = backups / destination.name
    if destination.is_symlink() or backup.is_symlink():
        raise SystemExit(f"Refusing symbolic-link desktop launcher or backup: {destination}")
    if destination.exists() and "X-UURB-Managed=true" not in destination.read_text(encoding="utf-8").splitlines():
        if name in ("x11vnc", "xfreerdp", "obconf"):
            continue
        backups.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(destination, backup)
    escaped = str(home).replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace("`", "\\`").replace("%", "%%")
    rendered = (templates / (name + ".desktop.in")).read_text(encoding="utf-8").replace("%HOME%", escaped)
    fd, temporary = tempfile.mkstemp(prefix=".uu-launcher-", dir=destination.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(rendered + "X-UURB-Managed=true\n")
            os.fchmod(stream.fileno(), 0o644)
        os.replace(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)
PY_TOOL_LAUNCHERS
if [[ -d "$HOME/Desktop" ]] && command -v gio >/dev/null 2>&1; then
    gio set "$HOME/Desktop/UU Remote.desktop" metadata::trusted true \
        >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" \
        >/dev/null 2>&1 || true
fi

tls_dir="$HOME/.local/share/gnome-remote-desktop"
tls_cert="$tls_dir/rdp-tls.crt"
tls_key="$tls_dir/rdp-tls.key"
mkdir -p "$tls_dir"
if [[ ! -s "$tls_cert" || ! -s "$tls_key" ]]; then
    "$openssl_bin" req -new -newkey rsa:3072 -days 730 -nodes -x509 \
        -subj "/CN=$(hostname) UU Remote bridge" \
        -keyout "$tls_key" -out "$tls_cert"
    chmod 0600 "$tls_key"
fi

rdp_password="$("$secret_tool_bin" lookup service uu-desktop-bridge \
    username "$bridge_user" || true)"
if [[ -z "$rdp_password" ]]; then
    while true; do
        read -rsp 'Password for the local GNOME RDP relay: ' rdp_password
        printf '\n'
        read -rsp 'Repeat the relay password: ' confirmation
        printf '\n'
        if [[ -n "$rdp_password" && "$rdp_password" == "$confirmation" ]]; then
            unset confirmation
            break
        fi
        printf 'Passwords did not match or were empty.\n' >&2
    done
fi

"$grdctl_bin" rdp set-port "$rdp_port"
"$grdctl_bin" rdp set-tls-cert "$tls_cert"
"$grdctl_bin" rdp set-tls-key "$tls_key"
printf '%s' "$rdp_password" | "$python_bin" \
    "$repo_dir/scripts/uu-rdp-credentials.py" "$bridge_user"
"$grdctl_bin" rdp disable-view-only
"$grdctl_bin" rdp disable-port-negotiation
"$grdctl_bin" rdp enable
printf '%s' "$rdp_password" | "$secret_tool_bin" store \
    --label='UU Remote Ubuntu bridge RDP credential' \
    service uu-desktop-bridge username "$bridge_user"
relay_vnc_auth_file="$config_dir/relay-vnc.pass"
relay_vnc_auth_temporary="$(mktemp "$config_dir/relay-vnc.pass.XXXXXX")"
relay_vnc_password="$(LC_ALL=C printf '%.8s' "$rdp_password")"
printf -v relay_vnc_auth_quoted '%q' "$relay_vnc_auth_temporary"
if ! printf '%s\n%s\ny\n' "$relay_vnc_password" "$relay_vnc_password" |
    /usr/bin/script -qefc \
        "/usr/bin/x11vnc -storepasswd $relay_vnc_auth_quoted" /dev/null \
        >/dev/null 2>&1; then
    rm -f "$relay_vnc_auth_temporary"
    printf 'Could not create the loopback VNC credential.\n' >&2
    exit 1
fi
chmod 0600 "$relay_vnc_auth_temporary"
mv -f "$relay_vnc_auth_temporary" "$relay_vnc_auth_file"
unset relay_vnc_auth_quoted
unset relay_vnc_password
unset rdp_password

"${systemctl_user[@]}" daemon-reload
"${systemctl_user[@]}" reenable uu-remote-bridge.service

if [[ "$fresh_install" == true && "$skip_account_login" == false ]]; then
    printf '\nUU Remote needs an authenticated account once.\n'
    if [[ "$ID" == debian ]]; then
        (
            # UU's launcher exits before its real GUI. Keep the Wine desktop
            # alive and let the user confirm sign-in before stopping the prefix.
            trap stop_wine_prefix EXIT
            unset WAYLAND_DISPLAY
            export XDG_SESSION_TYPE=x11
            export WINEDLLOVERRIDES='winebth.sys=d;winedbg.exe=d;mscoree,mshtml='
            "$wine_prefix/compat/winlogon.exe" \
                >>"$wine_prefix/compat/account-login.log" 2>&1 &
            login_winlogon_pid=$!
            (cd "$uu_dir" && "$wine_bin" GameViewer.exe)
            if ! kill -0 "$login_winlogon_pid" 2>/dev/null; then
                printf 'The account-login Wine desktop exited unexpectedly.\n' >&2
                exit 1
            fi
            read -rp 'Complete UU sign-in, then press Enter here to start the bridge: ' \
                login_confirmation
        )
    else
        printf 'Complete the official UU sign-in window, then close that window.\n'
        (cd "$uu_dir" && "$wine_bin" GameViewer.exe) || true
        stop_wine_prefix
    fi
fi

if [[ "$start_service" == true ]]; then
    "${systemctl_user[@]}" restart uu-remote-bridge.service
    "$repo_dir/scripts/verify.sh" --quick
fi

if [[ "$unattended" == true ]]; then
    "$repo_dir/scripts/configure-unattended.sh" enable
fi

if [[ "$automatic_updates" == true ]]; then
    "$repo_dir/scripts/configure-updater.sh" enable --repo "$repo_dir"
fi

printf '\nInstalled UU Remote Ubuntu bridge.\n'
printf 'Service: systemctl --user status uu-remote-bridge.service\n'
printf 'App:     open "UU Remote" or run uu-remote open\n'
printf 'Console: http://127.0.0.1:%s/vnc.html\n' "$console_web_port"
printf 'Logs:    uu-remote logs\n'

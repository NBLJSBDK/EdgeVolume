#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
    if [[ -z "${SUDO_USER:-}" || "$SUDO_USER" == "root" ]]; then
        echo "请从普通 KDE 用户运行此脚本；不要直接以 root 用户运行。" >&2
        exit 1
    fi

    ORIGINAL_UID="$(id -u "$SUDO_USER")"
    ORIGINAL_RUNTIME="/run/user/$ORIGINAL_UID"
    exec sudo -u "$SUDO_USER" -H env \
        XDG_RUNTIME_DIR="$ORIGINAL_RUNTIME" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$ORIGINAL_RUNTIME/bus" \
        bash "$0" "$@"
fi

BIN_PATH="$HOME/.local/bin/edge-volume"
KWIN_PATH="$HOME/.local/share/kwin/scripts/edge-volume-cursor"
AUTOSTART_PATH="$HOME/.config/autostart/edge-volume.desktop"
UDEV_RULE_PATH="/etc/udev/rules.d/99-edge-volume-mouse.rules"
EDGE_VOLUME_USER="$(id -un)"

echo "停止 EdgeVolume X11 后端……"
pkill -TERM -x edge-volume 2>/dev/null || true
for _ in {1..30}; do
    if ! pgrep -x edge-volume >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
if pgrep -x edge-volume >/dev/null 2>&1; then
    pkill -KILL -x edge-volume 2>/dev/null || true
    sleep 0.2
fi

rm -f "$BIN_PATH" "$AUTOSTART_PATH"
rm -rf "$KWIN_PATH"

if command -v kwriteconfig6 >/dev/null 2>&1; then
    kwriteconfig6 --file "$HOME/.config/kwinrc" \
        --group Plugins --key edge-volume-cursorEnabled --type bool false
fi
if command -v qdbus6 >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1 || true
fi

sudo -v
sudo rm -f "$UDEV_RULE_PATH"
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input --action=change

if command -v setfacl >/dev/null 2>&1; then
    for device in /dev/input/event*; do
        [[ -e "$device" ]] || continue
        sudo setfacl -x "u:${EDGE_VOLUME_USER}" "$device" 2>/dev/null || true
    done
    if [[ -e /dev/uinput ]]; then
        sudo setfacl -x "u:${EDGE_VOLUME_USER}" /dev/uinput 2>/dev/null || true
    fi
fi

failed=0
for path in "$BIN_PATH" "$AUTOSTART_PATH" "$KWIN_PATH" "$UDEV_RULE_PATH"; do
    if [[ -e "$path" || -L "$path" ]]; then
        echo "卸载检查失败：仍存在 $path" >&2
        failed=1
    fi
done
if pgrep -x edge-volume >/dev/null 2>&1; then
    echo "卸载检查失败：edge-volume 进程仍在运行。" >&2
    failed=1
fi
if grep -Fq 'EdgeVolume Virtual ' /proc/bus/input/devices 2>/dev/null; then
    echo "卸载检查失败：虚拟鼠标仍存在，请注销并重新登录。" >&2
    failed=1
fi
if [[ "$(kreadconfig6 --file "$HOME/.config/kwinrc" \
        --group Plugins --key edge-volume-cursorEnabled 2>/dev/null || true)" == "true" ]]; then
    echo "卸载检查失败：KWin 脚本配置仍为启用。" >&2
    failed=1
fi

if ((failed)); then
    echo "EdgeVolume X11 已执行卸载，但检查发现残留。" >&2
    exit 1
fi

echo "EdgeVolume X11 已卸载，检查通过。"

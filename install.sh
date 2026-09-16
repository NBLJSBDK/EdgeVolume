#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
    if [[ -z "${SUDO_USER:-}" || "$SUDO_USER" == "root" ]]; then
        echo "请从普通 KDE 用户运行此脚本；不要直接以 root 用户运行。" >&2
        exit 1
    fi

    # 即使用户执行了 sudo ./install.sh，也回到原来的 KDE 用户执行。
    # 这样 HOME、自启动项和 Session D-Bus 都属于正确的用户。
    ORIGINAL_UID="$(id -u "$SUDO_USER")"
    ORIGINAL_RUNTIME="/run/user/$ORIGINAL_UID"
    exec sudo -u "$SUDO_USER" -H env \
        XDG_RUNTIME_DIR="$ORIGINAL_RUNTIME" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$ORIGINAL_RUNTIME/bus" \
        bash "$0" "$@"
fi

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIN_DIR="$HOME/.local/bin"
KWIN_DIR="$HOME/.local/share/kwin/scripts/edge-volume-cursor"
AUTOSTART_DIR="$HOME/.config/autostart"
UDEV_RULE_PATH="/etc/udev/rules.d/99-edge-volume-mouse.rules"

if ! command -v cmake >/dev/null 2>&1; then
    echo "缺少 cmake" >&2
    exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel

install -Dm755 "$BUILD_DIR/edge-volume" "$BIN_DIR/edge-volume"

mkdir -p "$KWIN_DIR"
install -Dm644 "$ROOT_DIR/kwin-script/metadata.json" \
    "$KWIN_DIR/metadata.json"
install -Dm644 "$ROOT_DIR/kwin-script/contents/code/main.js" \
    "$KWIN_DIR/contents/code/main.js"

echo
echo "========== 配置鼠标输入权限 =========="

if command -v sudo >/dev/null 2>&1 && command -v udevadm >/dev/null 2>&1; then
    EDGE_VOLUME_USER="$(id -un)"
    RULE_TMP="$(mktemp)"
    trap 'rm -f "$RULE_TMP"' EXIT HUP INT TERM

    sed "s/@EDGE_VOLUME_USER@/$EDGE_VOLUME_USER/g" \
        "$ROOT_DIR/edge-volume-mouse.rules.in" > "$RULE_TMP"

    sudo install -o root -g root -m 0644 \
        "$RULE_TMP" "$UDEV_RULE_PATH"
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=input --action=change

    echo "已配置：$UDEV_RULE_PATH"
    echo "用户：$EDGE_VOLUME_USER"
else
    echo "警告：找不到 sudo 或 udevadm，跳过鼠标权限配置。" >&2
fi

mkdir -p "$AUTOSTART_DIR"
install -Dm644 "$ROOT_DIR/edge-volume.desktop.in" \
    "$AUTOSTART_DIR/edge-volume.desktop"
sed -i "s|@EDGE_VOLUME_EXEC@|$BIN_DIR/edge-volume|" \
    "$AUTOSTART_DIR/edge-volume.desktop"
chmod 644 "$AUTOSTART_DIR/edge-volume.desktop"

if command -v kwriteconfig6 >/dev/null 2>&1; then
    kwriteconfig6 --file kwinrc --group Plugins \
        --key edge-volume-cursorEnabled true
fi

if command -v qdbus6 >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure || true
fi

mkdir -p "$HOME/.local/state"
pkill -x edge-volume 2>/dev/null || true
nohup "$BIN_DIR/edge-volume" \
    >"$HOME/.local/state/edge-volume.log" 2>&1 &

sleep 1

echo
echo "安装完成。"
echo "程序：$BIN_DIR/edge-volume"
echo "KWin 脚本：$KWIN_DIR"
echo "日志：$HOME/.local/state/edge-volume.log"
echo
echo "注意：不要用 sudo 启动 edge-volume；它必须连接当前用户的 KDE D-Bus。"
echo "如果日志显示无法打开 /dev/input/event*，再单独处理输入设备权限。"

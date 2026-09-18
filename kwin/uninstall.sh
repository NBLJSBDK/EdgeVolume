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

PLUGIN_ID="kdevolume"
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_PATHS=()

add_plugin_path()
{
    local path="$1"
    [[ -e "$path" || -L "$path" ]] || return 0
    for existing in "${PLUGIN_PATHS[@]}"; do
        [[ "$existing" == "$path" ]] && return 0
    done
    PLUGIN_PATHS+=("$path")
}

if [[ -f "$ROOT_DIR/build/install_manifest.txt" ]]; then
    while IFS= read -r path; do
        [[ "$path" == */kwin/plugins/${PLUGIN_ID}.so ]] && add_plugin_path "$path"
    done < "$ROOT_DIR/build/install_manifest.txt"
fi

for root in "$HOME/.local/lib" /usr/lib /usr/local/lib; do
    [[ -d "$root" ]] || continue
    while IFS= read -r path; do
        add_plugin_path "$path"
    done < <(find "$root" -type f -path "*/kwin/plugins/${PLUGIN_ID}.so" 2>/dev/null)
done

if command -v qtpaths6 >/dev/null 2>&1; then
    qt_plugin_dir="$(qtpaths6 --plugin-dir 2>/dev/null || true)"
    [[ -n "$qt_plugin_dir" ]] && add_plugin_path "$qt_plugin_dir/kwin/plugins/${PLUGIN_ID}.so"
fi

echo "禁用 EdgeVolume-KWin……"
if command -v kwriteconfig6 >/dev/null 2>&1; then
    kwriteconfig6 --file "$HOME/.config/kwinrc" \
        --group Plugins --key kdevolumeEnabled --type bool false
fi
if command -v qdbus6 >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1 || true
fi

if ((${#PLUGIN_PATHS[@]} > 0)); then
    sudo -v
    for path in "${PLUGIN_PATHS[@]}"; do
        echo "删除：$path"
        sudo rm -f "$path"
    done
else
    echo "未找到已安装的 ${PLUGIN_ID}.so。"
fi

failed=0
for path in "${PLUGIN_PATHS[@]}"; do
    if [[ -e "$path" || -L "$path" ]]; then
        echo "卸载检查失败：仍存在 $path" >&2
        failed=1
    fi
done
if [[ "$(kreadconfig6 --file "$HOME/.config/kwinrc" \
        --group Plugins --key kdevolumeEnabled 2>/dev/null || true)" == "true" ]]; then
    echo "卸载检查失败：kdevolumeEnabled 仍为 true。" >&2
    failed=1
fi

for pid in $(pgrep -x kwin_wayland 2>/dev/null || true) \
           $(pgrep -x kwin_x11 2>/dev/null || true); do
    if grep -q "/${PLUGIN_ID}\.so" "/proc/$pid/maps" 2>/dev/null; then
        echo "提示：KWin 进程 $pid 仍加载旧插件，请注销并重新登录。" >&2
        failed=1
    fi
done

if ((failed)); then
    echo "EdgeVolume-KWin 已执行卸载，但检查发现残留。" >&2
    exit 1
fi

echo "EdgeVolume-KWin 已卸载，检查通过。"

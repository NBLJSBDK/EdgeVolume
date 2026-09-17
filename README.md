# EdgeVolume

KDE 下左边缘滚轮调节 KDE 音量工具。

EdgeVolume 不创建透明窗口，也不占用屏幕边缘的输入区域，因此左侧 1px 的点击、右键、拖拽和截图不会被拦截。

## 工作方式

- `edge-volume` 独占带滚轮能力的 Linux evdev 输入设备，并创建一个同等能力的虚拟鼠标；
- KWin Script 通过 KDE 的 `workspace.cursorPos` 同步当前光标位置；
- 光标位于虚拟屏幕最左侧 1px 时，滚轮事件被消费、不再传给下层，并调用 KDE 的音量快捷键；
- 物理鼠标的点击、移动、拖拽以及非边缘滚轮事件，都会通过虚拟鼠标转发；
- 音量 OSD、反馈音、步进和最大音量设置继续由 KDE 管理。

支持 KDE Plasma 的 X11 和 Wayland 会话。

## 安装

先安装依赖：

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev
```

在本项目目录运行：

```bash
./install.sh
```

也可以运行：

```bash
sudo ./install.sh
```

安装脚本会自动切回原来的 KDE 用户，把程序安装到用户目录；sudo 仅用于写入鼠标输入设备的 udev 权限规则、刷新规则和配置。不会把程序安装到 `/root`，也不会让后台程序以 root 运行。

## 测试

```bash
tail -n 50 "$HOME/.local/state/edge-volume.log"
```

正常应看到：

```text
KWin 光标桥接已连接
拦截滚轮设备：/dev/input/event...，点击/移动转发到虚拟鼠标
```

把鼠标移到屏幕最左侧，滚动滚轮测试音量；再测试最左侧像素的点击是否正常。

## 安装位置

- 程序：`~/.local/bin/edge-volume`
- KWin Script：`~/.local/share/kwin/scripts/edge-volume-cursor`
- 自启动：`~/.config/autostart/edge-volume.desktop`
- udev 规则：`/etc/udev/rules.d/99-edge-volume-mouse.rules`
- 日志：`~/.local/state/edge-volume.log`

## 卸载

```bash
pkill -x edge-volume 2>/dev/null || true
rm -f "$HOME/.local/bin/edge-volume"
rm -rf "$HOME/.local/share/kwin/scripts/edge-volume-cursor"
rm -f "$HOME/.config/autostart/edge-volume.desktop"
sudo rm -f /etc/udev/rules.d/99-edge-volume-mouse.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input --action=change
```

# EdgeVolume

KDE 下左边缘滚轮调节 KDE 音量工具。

同一个仓库提供两个后端：

| 会话 | 后端 | 目录 |
|---|---|---|
| X11 | EdgeVolume evdev 后端 | [`x11/`](x11/) |
| Wayland | EdgeVolume-KWin 原生插件 | [`kwin/`](kwin/) |

两个后端都只在鼠标位于虚拟屏幕最左侧 1px 时处理滚轮。普通点击、右键、拖拽和截图不会被透明窗口拦截。

## X11

使用 evdev 后端和虚拟鼠标转发物理鼠标事件：

```bash
cd x11
./install.sh
```

## Wayland

使用 KWin 输入过滤插件：

```bash
cd kwin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
sudo cmake --install build
kwriteconfig6 --file "$HOME/.config/kwinrc" \
    --group Plugins --key kdevolumeEnabled --type bool true
```

安装或更新后注销并重新登录 KDE，让 KWin 重新加载插件。

## 设计

- X11 后端独占带滚轮能力的 evdev 输入设备，并通过虚拟鼠标转发点击和移动。
- Wayland 后端直接运行在 KWin 输入管线中，不创建透明窗口、不读取 `/dev/input/event*`。
- 音量 OSD、提示音、步进和最大音量设置继续由 KDE 管理。

## 项目描述

KDE 下左边缘滚轮调节 KDE 音量，分别支持 X11 和 Wayland。

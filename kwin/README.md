# EdgeVolume-KWin

EdgeVolume 的 Wayland 后端：KDE Plasma 原生 KWin 输入过滤插件。鼠标位于虚拟屏幕最左侧 1px 时，滚轮调节 KDE 音量；点击、移动和拖拽不经过 EdgeVolume-KWin 处理。

第一版目标环境：Debian 13、KWin 6.3.6、Qt 6.8、Wayland。插件直接运行在 KWin 输入管线中，不创建透明窗口，不读取 `/dev/input/event*`，也不创建虚拟鼠标。X11 会话使用 EdgeVolume。

## 开发依赖

```bash
sudo apt update
sudo apt install kwin-dev extra-cmake-modules
```

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

## 安装测试

```bash
sudo cmake --install build
kwriteconfig6 --file "$HOME/.config/kwinrc" --group Plugins --key kdevolumeEnabled --type bool true
qdbus6 org.kde.KWin /KWin reconfigure
```

如果旧版 EdgeVolume 已安装，先停止它并移出 KDE 自启动目录，避免两个实现同时处理滚轮：

```bash
pkill -x edge-volume 2>/dev/null || true
mv "$HOME/.config/autostart/edge-volume.desktop" "$HOME/.config/edge-volume.desktop.disabled" 2>/dev/null || true
```

注销并重新登录 Wayland 会话后，检查左边缘滚轮行为。EdgeVolume-KWin 作为 KWin 插件运行，不会出现单独的进程。

## 设计

- KWin 的 `InputEventFilter::pointerAxis()` 接收滚轮事件；返回 `true` 消费事件，返回 `false` 放行。
- 只处理垂直轴，并排除触摸板 `Finger` / `Continuous` 滚动。
- 音量 OSD、提示音、步进和上限继续由 KDE 管理。
- EdgeVolume-KWin 只维护 Wayland 的 KWin 输入过滤路径；X11 会话使用仓库中的 `x11/` 后端。
- 插件接口属于 KWin 二进制扩展，需针对 KWin 版本重新编译。

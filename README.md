# KDEVolume

KDE Plasma 原生 KWin 输入过滤插件：鼠标位于虚拟屏幕最左侧 1px 时，滚轮调节 KDE 音量；点击、移动和拖拽不经过 KDEVolume 处理。

第一版目标环境：Debian 13、KWin 6.3.6、Qt 6.8。插件直接运行在 KWin 输入管线中，不创建透明窗口，不读取 `/dev/input/event*`，也不创建虚拟鼠标。

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
cmake --install build --prefix "$HOME/.local"
kwriteconfig6 --file kwinrc --group Plugins --key kdevolumeEnabled true
qdbus6 org.kde.KWin /KWin reconfigure
```

注销并重新登录后，检查 KWin 日志和左边缘滚轮行为。

## 设计

- KWin 的 `InputEventFilter::pointerAxis()` 接收滚轮事件；返回 `true` 消费事件，返回 `false` 放行。
- 只处理垂直轴，并排除触摸板 `Finger` / `Continuous` 滚动。
- 音量 OSD、提示音、步进和上限继续由 KDE 管理。
- 插件接口属于 KWin 二进制扩展，需针对 KWin 版本重新编译。

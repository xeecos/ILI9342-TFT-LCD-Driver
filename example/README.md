# Example GUI Sender

这个示例已经升级为一个 Windows GUI 应用，支持：

- 选择串口
- 拖拽 BMP/PPM/JPEG/PNG 图片到窗口
- 预览图片
- 点击 Send 发送 RGB565 数据帧
- 显示发送进度与状态

## 构建

在 Windows 下直接运行：

```bat
build.cmd gui
```

或者直接编译：

```bat
cl /EHsc /std:c++17 /O2 example\client.cpp /Feexample\client.exe /link comctl32.lib shell32.lib
```

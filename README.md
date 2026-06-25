# ILI9342-TFT-LCD-Driver

基于 CH32V305 的 ILI9342 LCD 驱动与 USB 图像显示项目。

## 当前实现进度
- 已完成基础 PlatformIO 工程配置
- 已补上 ILI9342 SPI 驱动骨架与初始化流程
- 已将主程序改为可编译的 LCD 测试显示入口
- 预留 USB 图像接收扩展接口，后续可继续接入 CDC 数据流

## 下一步计划
1. 接入 USB CDC 数据接收
2. 实现 RGB565 图像帧解析
3. 将接收到的像素数据写入 LCD 显示缓冲
4. 优化刷新速度与稳定性

## 示例上位机客户端

项目已在 [example](example) 目录中加入一个 Windows C++ 上位机客户端，用于把图片转换成 RGB565 数据并通过串口发送给单片机。

使用方式：

```bat
example\build.cmd
example\serial_image_client.exe --port COM3 --file test.bmp --size 320x240 --baud 115200 --verbose
```

更多说明请查看 [example/README.md](example/README.md)。

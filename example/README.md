# Example: C++ serial image sender

这个目录下提供了一个简单的 Windows 上位机客户端，用于把一张图片转换成 RGB565 数据并通过串口发送给 CH32V305 端。

## 功能特点

- 支持指定串口号，例如 COM3
- 支持指定图片文件路径
- 支持将图片缩放到目标尺寸
- 使用当前单片机侧的简单帧协议发送：
  - 0xAA 0x55
  - 命令 0x01
  - 宽度 / 高度
  - Payload 长度
  - RGB565 像素数据

## 构建

在 Windows 下使用 MSVC：

```bat
cl /EHsc /std:c++17 /O2 example\serial_image_client.cpp /Feexample\serial_image_client.exe
```

如果你使用 MinGW：

```bat
g++ -std=c++17 -O2 example\serial_image_client.cpp -o example\serial_image_client.exe
```

也可以直接运行：

```bat
example\build.cmd
```

## 使用示例

```bat
example\serial_image_client.exe --port COM3 --file test.bmp --size 320x240 --baud 115200 --verbose
```

如果你的串口名是 `\\.\COM10`，也可以直接写：

```bat
example\serial_image_client.exe --port \\.\COM10 --file test.bmp --size 320x240
```

## 支持的图片格式

- BMP
- PPM (P6)

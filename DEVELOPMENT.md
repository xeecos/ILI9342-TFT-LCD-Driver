# CH32V305 + USB 接收图像 + ILI9342 显示开发文档


## 1. 项目目标

基于 CH32V305 微控制器，实现以下功能：

- 通过 USB CDC 接收上位机发送的图像数据
- 将图像数据缓存到内存
- 使用 SPI 驱动 ILI9342 LCD 显示屏
- 屏幕分辨率为 320 × 240
- 支持 RGB565 格式显示

该方案适用于：

- 工业显示器
- 嵌入式 GUI 显示
- USB 图像传输演示平台

---

## 2. 硬件设计

### 2.1 目标硬件

- 主控：CH32V305
- LCD：ILI9342，320 × 240，RGB565
- 通信：USB CDC + SPI

### 2.2 引脚定义

| 功能 | CH32V305 引脚 | 说明 |
|---|---|---|
| LCD_RS | PB1 | 数据/命令切换 |
| LCD_RST | PC6 | 复位 |
| LCD_SDA | PB15 | SPI MOSI |
| LCD_SCL | PB13 | SPI SCK |
| LCD_CS | PB12 | SPI片选 |
| LCD_PWM | PA9 | 背光 PWM |
| USB_DP | PB7 | USB D+ |
| USB_DM | PB6 | USB D- |

### 2.3 推荐连接说明

- ILI9342 的 SPI 接线采用 4 线 SPI 模式
- SPI 模式建议使用 Mode 0：
  - CPOL = 0
  - CPHA = 0
- 速度建议从 8 MHz 起步，稳定后可尝试 16 MHz 或 18 MHz
- 背光通过 PA9 输出 PWM 控制，初始可直接拉高常亮

---

## 3. 软件架构

建议将工程分为以下模块：

1. USB CDC 模块
   
   - 初始化 USB 设备
   - 接收上位机发送的数据
   - 解析图像帧
2. 图像缓存模块
   
   - 分配 320 × 240 × 2 = 153600 字节缓存
   - 支持双缓冲，减少显示撕裂
3. ILI9342 驱动模块
   
   - 初始化 LCD
   - 写命令/写数据
   - 设置显示窗口
   - 写入 RGB565 像素数据
4. 显示刷新模块
   
   - 将缓存中的像素数据写入 LCD
   - 完成整屏刷新
5. 主循环
   
   - 轮询 USB 数据
   - 检测帧头
   - 解析后刷新屏幕

---

## 4. 图像传输协议设计

建议使用一个简单而稳健的帧协议。

### 4.1 帧头格式

| 字段 | 长度 | 说明 |
|---|---:|---|
| Start | 2 字节 | `0xAA 0x55` |
| Cmd | 1 字节 | `0x01` 表示图像帧 |
| Width | 2 字节 | 图像宽度，LE |
| Height | 2 字节 | 图像高度，LE |
| PayloadLen | 4 字节 | 图像数据长度，LE |
| CRC16 | 2 字节 | 数据校验 |

### 4.2 说明

- 对于 320 × 240 的 RGB565 图像：
  - 每像素 2 字节
  - 单帧大小 = 320 × 240 × 2 = 153600 字节
- 帧协议可保证接收端知道何时开始一帧、大小多少、是否出错

### 4.3 校验建议

- 推荐使用 CRC16
- 若校验失败，丢弃当前帧，不更新 LCD

---

## 5. ILI9342 初始化流程

### 5.1 初始化顺序

1. 拉低 RST，延时 10ms
2. 拉高 RST，延时 120ms
3. 发送初始化命令序列
4. 设置像素格式为 RGB565
5. 设置显示方向
6. 打开显示
7. 打开背光

### 5.2 关键命令

- `0x11`：Sleep Out
- `0x29`：Display On
- `0x3A`：Pixel Format Set，设置为 16-bit
- `0x36`：Memory Access Control
- `0x2A`：Column Address Set
- `0x2B`：Page Address Set
- `0x2C`：Memory Write

### 5.3 显示窗口设置

需要先设置显示窗口：

- 列地址：`0 ~ 319`
- 页地址：`0 ~ 239`

随后连续写入像素数据。

---

## 6. 关键驱动接口设计

建议实现以下函数：

```c
void lcd_init(void);
void lcd_write_cmd(uint8_t cmd);
void lcd_write_data(uint8_t data);
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_write_pixels(const uint8_t *buf, uint32_t len);
void lcd_fill_color(uint16_t color);
```

### 6.1 SPI 传输说明

- 片选拉低
- 发送命令或数据
- 片选拉高

### 6.2 背光控制

- PA9 可作为 PWM 输出
- 初版可直接输出高电平常亮
- 后续可加上亮度调节

---

## 7. USB CDC 接收流程

### 7.1 初始化

- 初始化 USB 全速设备
- 配置 CDC 设备类
- 注册接收回调
- 设置接收缓冲区

### 7.2 接收流程

1. USB 收到数据
2. 将数据写入环形缓冲区
3. 解析帧头
4. 若帧完整，提取图像数据
5. 校验通过后更新显示缓存
6. 调用 LCD 刷新

### 7.3 适配建议

- 建议使用 DMA 或双缓冲，避免 USB 接收与 LCD 刷新竞争
- 为了稳定性，建议单帧缓存使用 160 KB 以上

---

## 8. 上位机发送示例

下面给出一个 Python 示例，使用虚拟串口方式发送 RGB565 图像数据。实际 USB CDC 在 Windows 下通常会被识别为串口设备。

```python
import serial
import struct
from PIL import Image

ser = serial.Serial("COM3", 115200, timeout=1)

img = Image.open("test.jpg").convert("RGB")
img = img.resize((320, 240))

buf = bytearray()
for y in range(240):
    for x in range(320):
        r, g, b = img.getpixel((x, y))
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        buf.extend(struct.pack("<H", rgb565))

payload = bytes(buf)
width, height = 320, 240

header = struct.pack("<2sBHHI", b"\xAA\x55", 0x01, width, height, len(payload))
ser.write(header + payload)
ser.flush()
ser.close()
```

### 8.1 说明

- 这里使用 `Pillow` 读取并转换图片
- 实际发送时，建议在上位机端加入 CRC16 校验
- 如果你的 USB CDC 设备驱动名不同，请替换 `COM3`

---

## 9. 平台与工程说明

当前工程已配置为 CH32V305 目标平台，见 [platformio.ini](platformio.ini)：

- 平台：CH32V 平台
- 板型：genericCH32V305CCT6
- 框架：noneos-sdk
- 上传协议：wch-link

建议后续实现时将逻辑写入 [src/main.c](src/main.c) 或拆分为多个 C 文件。

---

## 10. 建议的开发步骤

1. 先实现 ILI9342 的基本 SPI 驱动
2. 再完成 LCD 初始化和整屏填色
3. 接着实现 USB CDC 接收与简单帧解析
4. 最后将接收到的 RGB565 数据写入 LCD

### 推荐优先级

- 第一步：显示一块纯色
- 第二步：显示一条水平/竖直测试线
- 第三步：显示一个简单的渐变图
- 第四步：显示完整图像

---

## 11. 常见问题与排查

### 11.1 屏幕无显示

- 检查 SPI 线是否接对
- 检查 CS、RST、DC 引脚是否稳定
- 检查背光是否开启
- 检查 SPI 时钟是否过快

### 11.2 USB 无法收到数据

- 检查 USB 设备枚举是否正常
- 检查 CDC 接收缓冲区是否足够
- 检查上位机是否发送完整帧
- 检查帧头和长度字段是否一致

### 11.3 图像显示异常

- 检查像素格式是否为 RGB565
- 检查显示方向是否正确
- 检查字节序是否一致
- 检查窗口设置是否正确

---

## 12. 结论

这套方案可行，且适合初期快速验证。对于 CH32V305 这类 MCU 而言，USB CDC + SPI + ILI9342 的组合是一个非常实用的嵌入式图像显示方案。

建议的初始目标：

- 先支持单帧显示
- 后续再扩展到连续刷新
- 如果需要更高刷新率，再考虑 DMA、双缓冲和压缩传输

如果你要继续，我下一步可以直接把这份文档进一步落实为可编译的 CH32V305 firmware 代码框架。
1. 先实现 ILI9342 的基本 SPI 驱动
2. 再完成 LCD 初始化和整屏填色
3. 接着实现 USB CDC 接收与简单帧解析
4. 最后将接收到的 RGB565 数据写入 LCD

### 推荐优先级

- 第一步：显示一块纯色
- 第二步：显示一条水平/竖直测试线
- 第三步：显示一个简单的渐变图
- 第四步：显示完整图像

---

## 11. 常见问题与排查

### 11.1 屏幕无显示

- 检查 SPI 线是否接对
- 检查 CS、RST、DC 引脚是否稳定
- 检查背光是否开启
- 检查 SPI 时钟是否过快

### 11.2 USB 无法收到数据

- 检查 USB 设备枚举是否正常
- 检查 CDC 接收缓冲区是否足够
- 检查上位机是否发送完整帧
- 检查帧头和长度字段是否一致

### 11.3 图像显示异常

- 检查像素格式是否为 RGB565
- 检查显示方向是否正确
- 检查字节序是否一致
- 检查窗口设置是否正确

---

## 12. 结论

这套方案可行，且适合初期快速验证。对于 CH32V305 这类 MCU 而言，USB CDC + SPI + ILI9342 的组合是一个非常实用的嵌入式图像显示方案。

建议的初始目标：

- 先支持单帧显示
- 后续再扩展到连续刷新
- 如果需要更高刷新率，再考虑 DMA、双缓冲和压缩传输

如果你要继续，我下一步可以直接把这份文档进一步落实为可编译的 CH32V305 firmware 代码框架。

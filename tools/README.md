# WCH-Link 烧录工具

本目录包含适用于 **CH32V305 + PlatformIO + WCH-Link** 的烧录脚本。

## 文件说明

| 文件 | 平台 | 说明 |
|------|------|------|
| `flash.bat` | Windows (CMD) | 批处理脚本 |
| `flash.ps1` | Windows (PowerShell) | PowerShell 脚本（支持参数） |
| `flash.sh` | Linux / macOS / Git Bash | Bash 脚本 |

## 前置要求

- [Python](https://www.python.org/) 3.x
- [PlatformIO Core](https://platformio.org/install/cli) (`pip install platformio`)
- [WCH-Link 驱动](http://www.wch.cn/download/WCH-LinkTool_ZIP.html) (Windows 需要)
- WCH-Link 调试器（硬件）

## 使用方法

### Windows — CMD

```batch
flash.bat         编译并烧录
flash.bat build   仅编译
flash.bat upload  编译并烧录
flash.bat erase   擦除芯片
flash.bat monitor 打开串口监视器
flash.bat clean   清理编译产物
```

### Windows — PowerShell

```powershell
.\flash.ps1                    # 编译并烧录
.\flash.ps1 -Action build      # 仅编译
.\flash.ps1 -Action upload     # 编译并烧录
.\flash.ps1 -Action erase      # 擦除芯片
.\flash.ps1 -Action monitor    # 打开串口监视器
.\flash.ps1 -Action clean      # 清理编译产物
```

### Linux / macOS / Git Bash

```bash
chmod +x flash.sh
./flash.sh         编译并烧录
./flash.sh build   仅编译
./flash.sh upload  编译并烧录
./flash.sh erase   擦除芯片
./flash.sh monitor 打开串口监视器
./flash.sh clean   清理编译产物
```

## 环境变量

可通过 `ENV` 环境变量指定 PlatformIO 环境（默认 `ch32vdev`）：

```bash
# Bash
ENV=ch32vdev ./flash.sh upload
```

```powershell
# PowerShell
$env:ENV = 'ch32vdev'
.\flash.ps1
```

## 注意事项

1. 首次使用需安装依赖：
   ```bash
   pio pkg install
   ```
2. 连接 WCH-Link 后检查设备是否识别（Windows 设备管理器查看）。
3. 如需指定烧录端口：
   ```bash
   pio run -e ch32vdev -t upload --upload-port <PORT>
   ```
4. 烧录失败时尝试：
   - 重新插拔 WCH-Link
   - 检查目标板供电
   - 检查接线（SWDIO/SWCLK/GND/3.3V）

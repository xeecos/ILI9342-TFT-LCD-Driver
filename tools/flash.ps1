<#
.SYNOPSIS
    WCH-Link 烧录脚本 — PowerShell
.DESCRIPTION
    适用于 CH32V305 + PlatformIO + WCH-Link 的编译与烧录工具。
.PARAMETER Action
    执行动作: build | upload (默认) | erase | monitor | clean
.PARAMETER Env
    PlatformIO 环境名称 (默认: ch32vdev)
.PARAMETER ProjectDir
    项目目录路径 (默认: 脚本所在目录的上级目录)
.EXAMPLE
    .\flash.ps1               # 编译并烧录
    .\flash.ps1 -Action build # 仅编译
    .\flash.ps1 -Action erase # 擦除芯片
#>

param(
    [ValidateSet('build', 'upload', 'erase', 'monitor', 'clean')]
    [string]$Action = 'upload',

    [string]$Env = 'ch32vdev',

    [string]$ProjectDir = ''
)

# 确定项目目录
if (-not $ProjectDir) {
    $ProjectDir = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
}

$PioCmd = 'python -m platformio'

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " ILI9342 TFT LCD — WCH-Link 烧录工具" -ForegroundColor Cyan
Write-Host " 目标芯片: CH32V305" -ForegroundColor Cyan
Write-Host " 环境:     $Env" -ForegroundColor Cyan
Write-Host " 动作:     $Action" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

Push-Location $ProjectDir

try {
    switch ($Action) {
        'build' {
            Write-Host "[*] 编译固件..." -ForegroundColor Yellow
            Invoke-Expression "$PioCmd run -e $Env"
            if ($LASTEXITCODE -ne 0) { throw "编译失败" }
            Write-Host "[OK] 编译完成" -ForegroundColor Green
        }

        'upload' {
            Write-Host "[*] 编译并烧录..." -ForegroundColor Yellow
            Invoke-Expression "$PioCmd run -e $Env -t upload"
            if ($LASTEXITCODE -ne 0) {
                throw @"
烧录失败
可能的原因:
  1. WCH-Link 未连接
  2. 驱动未安装
  3. 目标板未上电
"@
            }
            Write-Host "[OK] 烧录成功" -ForegroundColor Green
        }

        'erase' {
            Write-Host "[*] 擦除芯片..." -ForegroundColor Yellow
            Invoke-Expression "$PioCmd run -e $Env -t erase"
            if ($LASTEXITCODE -ne 0) { throw "擦除失败" }
            Write-Host "[OK] 擦除完成" -ForegroundColor Green
        }

        'monitor' {
            Write-Host "[*] 打开串口监视器..." -ForegroundColor Yellow
            Write-Host "提示: 按 Ctrl+C 退出" -ForegroundColor Magenta
            Invoke-Expression "$PioCmd device monitor -e $Env"
        }

        'clean' {
            Write-Host "[*] 清理编译产物..." -ForegroundColor Yellow
            Invoke-Expression "$PioCmd run -e $Env -t clean"
            if ($LASTEXITCODE -ne 0) { throw "清理失败" }
            Write-Host "[OK] 清理完成" -ForegroundColor Green
        }
    }
}
catch {
    Write-Host "[FAIL] $_" -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "完成。" -ForegroundColor Green

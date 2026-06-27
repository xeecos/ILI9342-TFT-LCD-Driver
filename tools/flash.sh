#!/usr/bin/env bash
# ============================================================================
#  WCH-Link 烧录脚本 — Bash
#  适用于 CH32V305 + PlatformIO + WCH-Link
#  用法: ./flash.sh [选项]
#  选项:
#    build      仅编译，不烧录
#    upload     编译并烧录（默认）
#    erase      擦除芯片
#    monitor    打开串口监视器
#    clean      清理编译产物
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PIO_CMD="${PIO_CMD:-python -m platformio}"
ENV="${ENV:-ch32vdev}"

ACTION="${1:-upload}"

echo "========================================"
echo " ILI9342 TFT LCD — WCH-Link 烧录工具"
echo " 目标芯片: CH32V305"
echo " 环境:     $ENV"
echo " 动作:     $ACTION"
echo "========================================"
echo ""

cd "$PROJECT_DIR"

case "$ACTION" in
    build)
        echo "[*] 编译固件..."
        $PIO_CMD run -e "$ENV"
        echo "[OK] 编译完成"
        ;;

    upload)
        echo "[*] 编译并烧录..."
        $PIO_CMD run -e "$ENV" -t upload
        echo "[OK] 烧录成功"
        ;;

    erase)
        echo "[*] 擦除芯片..."
        $PIO_CMD run -e "$ENV" -t erase
        echo "[OK] 擦除完成"
        ;;

    monitor)
        echo "[*] 打开串口监视器..."
        echo "提示: 按 Ctrl+C 退出"
        $PIO_CMD device monitor -e "$ENV"
        ;;

    clean)
        echo "[*] 清理编译产物..."
        $PIO_CMD run -e "$ENV" -t clean
        echo "[OK] 清理完成"
        ;;

    *)
        echo "用法: $0 {build|upload|erase|monitor|clean}"
        exit 1
        ;;
esac

cd "$SCRIPT_DIR"
echo ""
echo "完成。"

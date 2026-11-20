#!/usr/bin/env bash
# A1BOX 固件命令行。用法见 README.md。
#   ./a1box.sh test      在电脑上跑逻辑测试（不需要硬件）
#   ./a1box.sh config    检查配置
#   ./a1box.sh compile   只编译
#   ./a1box.sh run       编译并烧录（第一次插 USB，以后自动走 WiFi）
#   ./a1box.sh logs      看主控日志
set -euo pipefail
cd "$(dirname "$0")"

ESPHOME="$HOME/.local/share/a1box-esphome/venv/bin/esphome"
# ESP-IDF 工具链默认从 GitHub 下载，国内连不上，改走乐鑫的国内镜像
export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"

case "${1:-}" in
  test)
    g++ -std=c++17 -Wall -Wextra -Werror -I. test/test_logic.cpp -o /tmp/a1box_test
    /tmp/a1box_test
    ;;
  config | compile | upload | run | logs | clean)
    cmd="$1"
    shift
    exec "$ESPHOME" "$cmd" a1box.yaml "$@"
    ;;
  *)
    echo "用法：$0 test|config|compile|run|logs" >&2
    exit 1
    ;;
esac

#!/bin/bash
# cleanup_network.sh - 清理网络模拟配置

set -e

# 检查权限
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)"
   exit 1
fi

echo "Cleaning up network simulation..."

# 清理dnctl管道
dnctl -q flush 2>/dev/null || true

# 恢复默认pf配置
pfctl -f /etc/pf.conf 2>/dev/null || true

# 删除临时规则文件
rm -f /tmp/xquic_test.pf.conf

echo "[OK] Network simulation cleaned up"
echo "Network is back to normal state"

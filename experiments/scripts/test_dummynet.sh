#!/bin/bash
# test_dummynet.sh - 测试macOS loopback是否支持dummynet
# 需要sudo权限

set -e

if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)"
   exit 1
fi

echo "========================================="
echo "   Testing macOS Dummynet on Loopback"
echo "========================================="

# 清理
echo "1. Cleaning up..."
pfctl -f /etc/pf.conf 2>/dev/null || true
dnctl -q flush 2>/dev/null || true

# 测试baseline ping
echo ""
echo "2. Baseline ping test (no delay)..."
ping -c 3 127.0.0.1 | grep "round-trip"

# 创建100ms延迟的管道
echo ""
echo "3. Creating dummynet pipe with 100ms delay..."
dnctl pipe 1 config delay 100ms
dnctl show pipe

# 创建并加载pf规则
echo ""
echo "4. Creating pf rules..."
PF_TEST="/tmp/pf_test.conf"
cat > "$PF_TEST" << 'EOF'
# Test dummynet rules
scrub-anchor "com.apple/*"
nat-anchor "com.apple/*"
rdr-anchor "com.apple/*"
dummynet-anchor "com.apple/*"

# Apply dummynet to ALL loopback traffic
dummynet in quick on lo0 all pipe 1
dummynet out quick on lo0 all pipe 1

pass in all
pass out all
EOF

pfctl -f "$PF_TEST" 2>/dev/null
pfctl -e 2>/dev/null || true

echo ""
echo "5. Verifying pf rules..."
pfctl -s rules | grep -i dummynet || echo "WARNING: No dummynet rules found!"

# 测试ping with delay
echo ""
echo "6. Testing ping WITH dummynet (should show ~200ms RTT - 100ms each way)..."
ping -c 3 127.0.0.1 | grep "round-trip"

# 清理
echo ""
echo "7. Cleaning up..."
pfctl -f /etc/pf.conf 2>/dev/null ||true
dnctl -q flush 2>/dev/null || true

echo ""
echo "========================================="
echo "Test complete!"
echo "If step 6 showed ~200ms RTT, dummynet WORKS on loopback"
echo "If step 6 showed same RTT as step 2, dummynet DOES NOT WORK on loopback"
echo "========================================="

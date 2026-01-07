#!/bin/bash
# setup_network.sh - 配置网络模拟环境（macOS）
# 需要管理员权限

set -e

PROFILE_FILE="$1"

if [[ -z "$PROFILE_FILE" ]] || [[ ! -f "$PROFILE_FILE" ]]; then
    echo "Usage: sudo $0 <profile_file>"
    echo "Example: sudo $0 ../network_profiles/high_latency.profile"
    exit 1
fi

# 检查权限
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)"
   exit 1
fi

# 读取配置文件
source "$PROFILE_FILE"

echo "========================================="
echo "   Network Profile Setup"
echo "========================================="
echo "Profile: $(basename $PROFILE_FILE .profile)"
echo "Latency: ${LATENCY_MS}ms"
echo "Packet Loss: ${PACKET_LOSS_PCT}%"
echo "Bandwidth: ${BANDWIDTH_MBPS}Mbps"
echo "Jitter: ${JITTER_MS}ms"
echo "========================================="

# macOS使用dnctl进行网络模拟
# 清理现有规则
pfctl -f /etc/pf.conf 2>/dev/null || true
dnctl -q flush 2>/dev/null || true

# 创建dummynet管道
PIPE_NUM=1

# 配置管道参数
PIPE_CONFIG="pipe ${PIPE_NUM} config"

# 带宽限制
if [[ -n "$BANDWIDTH_MBPS" ]] && [[ "$BANDWIDTH_MBPS" != "0" ]]; then
    PIPE_CONFIG+=" bw ${BANDWIDTH_MBPS}Mbit/s"
fi

# 延迟
if [[ -n "$LATENCY_MS" ]] && [[ "$LATENCY_MS" != "0" ]]; then
    PIPE_CONFIG+=" delay ${LATENCY_MS}ms"
fi

# 丢包率
if [[ -n "$PACKET_LOSS_PCT" ]] && [[ "$PACKET_LOSS_PCT" != "0" ]]; then
    # 转换为0-1的小数
    LOSS_PROB=$(echo "scale=4; $PACKET_LOSS_PCT / 100" | bc)
    PIPE_CONFIG+=" plr ${LOSS_PROB}"
fi

# 创建管道
dnctl $PIPE_CONFIG

# 配置pf规则
PF_RULES="/tmp/xquic_test.pf.conf"
cat > "$PF_RULES" << EOF
# XQUIC Test Network Simulation Rules
# 对loopback接口应用流量控制

# 基本规则
scrub-anchor "com.apple/*"
nat-anchor "com.apple/*"
rdr-anchor "com.apple/*"
dummynet-anchor "com.apple/*"

# XQUIC测试规则 - 【关键修复】只对outgoing流量应用，避免双重处理
# 端口4433是XQUIC server端口，5555是UDP对比端口
dummynet out quick on lo0 proto udp from any port 4433 to any pipe ${PIPE_NUM}
dummynet out quick on lo0 proto udp from any to any port 5555 pipe ${PIPE_NUM}

# 放行其他流量
pass in all
pass out all
EOF

# 加载pf规则
pfctl -f "$PF_RULES" 2>/dev/null
pfctl -e 2>/dev/null || true  # 启用pf（如果已启用会报错但无妨）

echo ""
echo "[OK] Network simulation configured successfully"
echo ""
echo "Active dummynet pipes:"
dnctl show pipe
echo ""
echo "Active pf rules:"
pfctl -s rules 2>/dev/null | grep -E "dummynet|4433|5555" || echo "(no rules shown, but should be active)"
echo ""
echo "To disable network simulation, run: sudo ./cleanup_network.sh"

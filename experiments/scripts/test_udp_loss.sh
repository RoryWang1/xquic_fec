#!/bin/bash
# test_udp_loss.sh - 测试UDP端口4433的实际丢包率

set -e

PORT=4433
COUNT=100

echo "Testing UDP packet loss on port $PORT..."
echo "Sending $COUNT packets..."

# 启动UDP接收器（后台）
nc -u -l $PORT | tee /tmp/udp_received.txt &
NC_PID=$!

sleep 1

# 发送COUNT个包，每个包有唯一序号
for i in $(seq 1 $COUNT); do
    echo "Packet $i" | nc -u -w 0 127.0.0.1 $PORT
    sleep 0.01  # 10ms间隔
done

# 等待接收完成
sleep 2

# 终止接收器
kill $NC_PID 2>/dev/null || true

# 统计接收到的包数
RECEIVED=$(wc -l < /tmp/udp_received.txt)

echo ""
echo "========================================="
echo "UDP Packet Loss Test Results"
echo "========================================="
echo "Sent: $COUNT packets"
echo "Received: $RECEIVED packets"
echo "Loss Rate: $(( (COUNT - RECEIVED) * 100 / COUNT ))%"
echo ""
echo "Expected: ~5% loss (95 packets)"
echo "Actual: ${RECEIVED}/${COUNT} packets"
echo "========================================="

rm -f /tmp/udp_received.txt

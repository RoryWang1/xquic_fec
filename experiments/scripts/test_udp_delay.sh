#!/bin/bash
# test_udp_delay.sh - 测试UDP端口5555是否被dummynet影响

echo "Testing if dum

mynet affects UDP port 5555..."
echo "Current dummynet configuration should have 50ms delay + 5% loss"
echo ""

# 启动UDP接收器（后台）
nc -u -l 5555 > /dev/null 2>&1 &
NC_PID=$!
sleep 1

# 发送UDP包并测量时间
echo "Sending UDP packets to localhost:5555..."
time (for i in {1..10}; do
    echo "test" | nc -u -w 0 127.0.0.1 5555
done)

# 清理
kill $NC_PID 2>/dev/null || true

echo ""
echo "If dummynet is working, this should take longer due to 50ms delay"
echo "Expected: ~500ms for 10 packets (50ms × 10)"

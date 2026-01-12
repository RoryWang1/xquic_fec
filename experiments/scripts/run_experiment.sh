#!/bin/bash
# run_experiment.sh - 运行单个实验场景
# Usage: ./run_experiment.sh <scenario_name> <duration_seconds>

set -e

SCENARIO="$1"
DURATION="${2:-60}"

if [[ -z "$SCENARIO" ]]; then
    echo "Usage: $0 <scenario_name> [duration_seconds]"
    echo ""
    echo "Available scenarios:"
    for profile in ../network_profiles/*.profile; do
        basename "$profile" .profile
    done
    exit 1
fi

# 路径设置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPERIMENT_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$EXPERIMENT_DIR")"
PROFILE_FILE="${EXPERIMENT_DIR}/network_profiles/${SCENARIO}.profile"
RESULTS_DIR="${EXPERIMENT_DIR}/results/${SCENARIO}/$(date +%Y%m%d_%H%M%S)"

# 检查profile文件
if [[ ! -f "$PROFILE_FILE" ]]; then
    echo "Error: Profile not found: $PROFILE_FILE"
    exit 1
fi

# 创建结果目录
mkdir -p "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR/screenshots"
mkdir -p "$RESULTS_DIR/logs"

echo "========================================="
echo "   XQUIC Performance Experiment"
echo "========================================="
echo "Scenario: $SCENARIO"
echo "Duration: ${DURATION}s"
echo "Results:  $RESULTS_DIR"
echo "========================================="

# 读取并显示网络配置
source "$PROFILE_FILE"
echo "Network Configuration:"
echo "  Latency: ${LATENCY_MS}ms"
echo "  Packet Loss: ${PACKET_LOSS_PCT}%"
echo "  Bandwidth: ${BANDWIDTH_MBPS}Mbps"
echo "  Description: ${DESCRIPTION}"
echo "========================================="

# 保存配置
cp "$PROFILE_FILE" "$RESULTS_DIR/network_config.profile"

# 设置网络模拟
echo "[1/5] Setting up network simulation..."
if [[ "$SCENARIO" != "baseline" ]]; then
    echo "Requesting sudo for network configuration..."
    sudo "${SCRIPT_DIR}/setup_network.sh" "$PROFILE_FILE"
else
    echo "Baseline scenario - no network simulation needed"
fi

# Cleanup函数
cleanup() {
    echo ""
    echo "Cleaning up..."
    
    # 优雅地关闭dual_player（给它时间关闭SDL窗口）
    if [[ -n "$PLAYER_PID" ]] && kill -0 $PLAYER_PID 2>/dev/null; then
        echo "Stopping player..."
        kill -TERM $PLAYER_PID 2>/dev/null || true
        # 等待最多3秒让它优雅退出
        for i in {1..30}; do
            if ! kill -0 $PLAYER_PID 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        # 如果还没退出，强制kill
        kill -9 $PLAYER_PID 2>/dev/null || true
    fi
    
    # 停止其他进程（这些不需要优雅退出）
    pkill -f camera_server || true
    pkill -f camera_client || true
    pkill -f ffmpeg || true
    
    # 清理FIFOs
    rm -f /tmp/camera_fifo /tmp/raw_left /tmp/raw_right
    
    # 恢复网络
    if [[ "$SCENARIO" != "baseline" ]]; then
        echo "Restoring network settings..."
        sudo "${SCRIPT_DIR}/cleanup_network.sh"
    fi
    
    echo "Cleanup complete"
}

trap cleanup EXIT INT TERM

# 编译项目
echo "[2/5] Building project..."
cd "${PROJECT_ROOT}/build"
cmake .. > /dev/null
make -j4 > /dev/null
echo "Build complete"

# 启动服务器
echo "[3/5] Starting camera server..."
"${PROJECT_ROOT}/build/camera_server" > "${RESULTS_DIR}/logs/server.log" 2>&1 &
SERVER_PID=$!
sleep 2

# 检查服务器是否启动成功
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: Server failed to start"
    cat "${RESULTS_DIR}/logs/server.log"
    exit 1
fi

# 创建原始视频FIFOs（在启动任何进程之前）
mkfifo /tmp/raw_left 2>/dev/null || true
mkfifo /tmp/raw_right 2>/dev/null || true

# 先启动播放器，让它打开FIFO准备读取
echo "[4/5] Starting dual-stream player (opens FIFOs)..."
"${PROJECT_ROOT}/build/dual_player" /tmp/raw_left /tmp/raw_right &
PLAYER_PID=$!

# 等待player打开FIFOs
sleep 2

# 现在启动视频源和解码器
echo "Starting video source and decoders..."
FIFO_PATH="/tmp/camera_fifo"
ffmpeg -y -f avfoundation -framerate 30 -video_size 640x480 -i "0" \
       -filter_complex "[0:v]split=2[to_xquic][to_local]" \
       -map "[to_xquic]" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 1000k -f mpegts "${FIFO_PATH}" \
       -map "[to_local]" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 1000k -f mpegts "udp://127.0.0.1:5555?pkt_size=1316" \
       > "${RESULTS_DIR}/logs/ffmpeg_source.log" 2>&1 &
SOURCE_PID=$!

# 立即启动UDP接收器（最小化初始丢包）- 使用listen模式
ffmpeg -y -f mpegts \
       -fflags nobuffer -flags low_delay -strict experimental \
       -probesize 32 -analyzeduration 0 \
       -i "udp://127.0.0.1:5555?listen&pkt_size=1316&overrun_nonfatal=1&fifo_size=500000" \
       -vf "drawtext=fontfile=/System/Library/Fonts/Helvetica.ttc:text='Original UDP':fontsize=30:fontcolor=white:x=20:y=20:box=1:boxcolor=black@0.5" \
       -c:v rawvideo -pix_fmt yuv420p -f rawvideo "/tmp/raw_left" \
       > "${RESULTS_DIR}/logs/decoder_left.log" 2>&1 &
DEC_LEFT_PID=$!

# 启动XQUIC解码器
# 大幅增加等待时间以容忍严重网络延迟
"${PROJECT_ROOT}/build/camera_client" 2> "${RESULTS_DIR}/logs/client.log" | \
ffmpeg -y -f mpegts \
       -thread_queue_size 1024 \
       -fflags +genpts+igndts -flags low_delay -strict experimental \
       -probesize 10000000 -analyzeduration 10000000 \
       -avoid_negative_ts make_zero \
       -i pipe:0 \
       -vf "drawtext=fontfile=/System/Library/Fonts/Helvetica.ttc:text='XQUIC':fontsize=30:fontcolor=white:x=20:y=20:box=1:boxcolor=black@0.5" \
       -c:v rawvideo -pix_fmt yuv420p -f rawvideo "/tmp/raw_right" \
       > "${RESULTS_DIR}/logs/decoder_right.log" 2>&1 &
DEC_RIGHT_PID=$!

sleep 1

echo ""
echo "========================================="
echo "   EXPERIMENT RUNNING"
echo "========================================="
echo "Duration: ${DURATION} seconds"
echo "Monitor the video window for visual comparison"
echo ""
echo "Collecting metrics..."

# 记录开始时间
START_TIME=$(date +%s)

# 创建metrics文件
METRICS_FILE="${RESULTS_DIR}/metrics.json"
cat > "$METRICS_FILE" << EOF
{
  "scenario": "$SCENARIO",
  "duration_seconds": $DURATION,
  "start_time": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "network_config": {
    "latency_ms": $LATENCY_MS,
    "packet_loss_pct": $PACKET_LOSS_PCT,
    "bandwidth_mbps": $BANDWIDTH_MBPS,
    "jitter_ms": $JITTER_MS
  },
  "samples": []
}
EOF

# 监控循环
SAMPLE_INTERVAL=5
elapsed=0

while [[ $elapsed -lt $DURATION ]]; do
    # 收集进程状态
    TIMESTAMP=$(date +%s)
    
    # 检查进程是否还在运行
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "WARNING: Server died!"
    fi
    
    if ! kill -0 $PLAYER_PID 2>/dev/null; then
        echo "WARNING: Player died!"
        break
    fi
    
    # TODO: 收集更多指标（CPU、网络统计等）
    # 这里可以扩展收集CPU使用率、内存、网络流量等
    
    sleep $SAMPLE_INTERVAL
    elapsed=$((elapsed + SAMPLE_INTERVAL))
    echo "Progress: ${elapsed}/${DURATION}s"
done

# 实验完成
END_TIME=$(date +%s)
ACTUAL_DURATION=$((END_TIME - START_TIME))

echo ""
echo "========================================="
echo "   EXPERIMENT COMPLETE"
echo "========================================="
echo "Actual duration: ${ACTUAL_DURATION}s"
echo "Results saved to: $RESULTS_DIR"
echo ""

# 更新metrics文件
python3 << PYTHON_EOF
import json
with open('$METRICS_FILE', 'r') as f:
    data = json.load(f)
data['end_time'] = '$(date -u +%Y-%m-%dT%H:%M:%SZ)'
data['actual_duration_seconds'] = $ACTUAL_DURATION
with open('$METRICS_FILE', 'w') as f:
    json.dump(data, f, indent=2)
PYTHON_EOF

# 显示日志摘要
echo "Log files:"
echo "  Server:  ${RESULTS_DIR}/logs/server.log"
echo "  Client:  ${RESULTS_DIR}/logs/client.log"
echo "  Left:    ${RESULTS_DIR}/logs/decoder_left.log"
echo "  Right:   ${RESULTS_DIR}/logs/decoder_right.log"
echo ""

# 提取关键信息到摘要
echo "Creating summary..."
cat > "${RESULTS_DIR}/summary.txt" << SUMMARY_EOF
XQUIC Performance Experiment Summary
=====================================

Scenario: $SCENARIO
Description: $DESCRIPTION

Network Configuration:
  - Latency: ${LATENCY_MS}ms
  - Packet Loss: ${PACKET_LOSS_PCT}%
  - Bandwidth: ${BANDWIDTH_MBPS}Mbps
  - Jitter: ${JITTER_MS}ms

Test Duration: ${ACTUAL_DURATION}s
Start Time: $(date -d "@$START_TIME" 2>/dev/null || date -r "$START_TIME")

Results Location: $RESULTS_DIR

Next Steps:
  1. Review logs in logs/ directory
  2. Run analysis: ./tools/analyze_results.py results/$SCENARIO/
  3. Compare with other scenarios

=====================================
SUMMARY_EOF

cat "${RESULTS_DIR}/summary.txt"

echo ""
echo "Experiment complete! Check the results directory for details."

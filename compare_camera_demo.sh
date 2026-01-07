#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
FIFO_PATH="/tmp/camera_fifo"
LOG_DIR="${PROJECT_ROOT}/logs"

echo "========================================="
echo "   XQUIC Video Comparison Demo"
echo "   Left: Original Source | Right: XQUIC Stream"
echo "========================================="

# 1. Cleanup
pkill -f camera_server || true
pkill -f camera_client || true
pkill -f ffmpeg || true
pkill -f ffplay || true

# 2. Build (Fast check)
cd "${BUILD_DIR}"
make -j4 > /dev/null
echo "[INFO] Build checked."

# 3. Start Server
echo "[INFO] Starting Camera Server..."
"${BUILD_DIR}/camera_server" > "${LOG_DIR}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

# 4. Start Source (Camera -> FIFO & UDP Local)
echo "[INFO] Starting Camera Source..."
ffmpeg -y -f avfoundation -framerate 30 -video_size 640x480 -i "0" \
       -filter_complex "[0:v]split=2[to_xquic][to_local]" \
       -map "[to_xquic]" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 2000k -f mpegts "${FIFO_PATH}" \
       -map "[to_local]" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 2000k -f mpegts "udp://127.0.0.1:5555?pkt_size=1316" > "${LOG_DIR}/ffmpeg_source.log" 2>&1 &
SOURCE_PID=$!

# 5. Start Decoders & Dual Player
echo "[INFO] Starting Independent Stream Decoders..."

# Create raw video FIFOs
mkfifo /tmp/raw_left 2>/dev/null || true
mkfifo /tmp/raw_right 2>/dev/null || true

# Left Decoder (Original UDP -> Raw YUV)
# Note: Outputting raw YUV420P to FIFO for the C Player
# Added drawtext for clear labelling
ffmpeg -y -f mpegts \
       -fflags nobuffer -flags low_delay -strict experimental \
       -probesize 32 -analyzeduration 0 \
       -i "udp://127.0.0.1:5555?listen&pkt_size=1316&overrun_nonfatal=1&fifo_size=500000" \
       -vf "drawtext=fontfile=/System/Library/Fonts/Helvetica.ttc:text='Original (UDP/Direct)':fontsize=30:fontcolor=white:x=20:y=20:box=1:boxcolor=black@0.5" \
       -c:v rawvideo -pix_fmt yuv420p -f rawvideo "/tmp/raw_left" > "${LOG_DIR}/dec_left.log" 2>&1 &
DEC_LEFT_PID=$!

sleep 1

# Right Decoder (XQUIC Pipe -> Raw YUV)
# Optimized for Low Latency: Minimal probing (we know it's MPEG-TS)
"${BUILD_DIR}/camera_client" 2> "${LOG_DIR}/client.log" | \
ffmpeg -y -f mpegts \
       -fflags nobuffer -flags low_delay -strict experimental \
       -probesize 32 -analyzeduration 0 \
       -i pipe:0 \
       -vf "drawtext=fontfile=/System/Library/Fonts/Helvetica.ttc:text='XQUIC (FEC/Reliable)':fontsize=30:fontcolor=white:x=20:y=20:box=1:boxcolor=black@0.5" \
       -c:v rawvideo -pix_fmt yuv420p -f rawvideo "/tmp/raw_right" > "${LOG_DIR}/dec_right.log" 2>&1 &
DEC_RIGHT_PID=$!

sleep 1

echo "[INFO] Starting Dual-Stream Independent Player (C/SDL2)..."
# Usage: ./dual_player <left_fifo> <right_fifo>
"${BUILD_DIR}/dual_player" /tmp/raw_left /tmp/raw_right &
PLAYER_PID=$!

echo "========================================="
echo "COMPARE DEMO RUNNING"
echo "Method: Multi-Threaded SDL2 Player (Custom C Code)"
echo "Mode:   Single Window, Independent Latency (Decoupled)"
echo "========================================="

# Trap to kill the new PID set
trap "kill $SERVER_PID $SOURCE_PID $DEC_LEFT_PID $DEC_RIGHT_PID $PLAYER_PID 2>/dev/null; rm /tmp/raw_left /tmp/raw_right" EXIT

wait $SERVER_PID

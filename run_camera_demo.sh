#!/bin/bash
set -e

# 设置项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
FIFO_PATH="/tmp/camera_fifo"

echo "========================================="
echo "   XQUIC 视频流实时传输演示启动脚本"
echo "========================================="

# 1. 清理旧进程
echo "[INFO] 清理旧进程..."
pkill -f camera_server || true
pkill -f camera_client || true
pkill -f ffmpeg || true
pkill -f ffplay || true

# 2. 编译项目
echo "[INFO] 开始编译..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake .. > /dev/null
make -j4
if [ $? -ne 0 ]; then
    echo "[ERROR] 编译失败！"
    exit 1
fi
echo "[INFO] 编译成功。"

# 3. 启动服务器
echo "[INFO] 启动 Camera Server..."
# 服务器会在后台运行，监听 4433 端口，并读取 /tmp/camera_fifo
"${BUILD_DIR}/camera_server" > "${PROJECT_ROOT}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1 # 等待服务器初始化
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "[ERROR] 服务器启动失败，请检查 server.log"
    exit 1
fi
echo "[SUCCESS] 服务器已启动 (PID: $SERVER_PID)"

# 4. 启动模拟摄像头 (FFmpeg)
echo "[INFO] 启动模拟摄像头 (FFmpeg)..."
echo "[INFO] 正在将测试视频流写入 FIFO: ${FIFO_PATH}"
# 使用真实摄像头 (MacBook Pro Camera) 生成视频，编码为 H.264，格式为 MPEG-TS，写入 FIFO
# 注意：首次运行时可能会弹出摄像头权限请求
# 优化：提升至 720p 分辨率，设置 5Mbps 码率，使用 high profile 和 veryfast preset
ffmpeg -y -f avfoundation -framerate 30 -video_size 1280x720 -i "0" \
       -c:v libx264 -preset veryfast -tune zerolatency -profile:v high \
       -b:v 5000k -maxrate 5000k -bufsize 10000k -pix_fmt yuv420p \
       -f mpegts "${FIFO_PATH}" > "${PROJECT_ROOT}/ffmpeg.log" 2>&1 &
FFMPEG_PID=$!
sleep 1
if ! kill -0 $FFMPEG_PID 2>/dev/null; then
    echo "[ERROR] FFmpeg 启动失败，请检查 ffmpeg.log"
    kill $SERVER_PID
    exit 1
fi
echo "[SUCCESS] 摄像头已启动 (PID: $FFMPEG_PID)"

# 5. 启动客户端并播放
echo "[INFO] 启动 Camera Client 并连接 FFplay..."
echo "[INFO] 客户端连接服务器 -> 接收流 -> 管道 -> FFplay 播放"
echo "========================================="
echo "正在播放... (按 Ctrl+C 停止)"
echo "========================================="

# 客户端连接服务器，将接收到的数据输出到 stdout
# FFplay 从 stdin 读取数据并播放
"${BUILD_DIR}/camera_client" 2> "${PROJECT_ROOT}/client.log" | \
ffplay -i - -fflags nobuffer -flags low_delay -framedrop -window_title "XQUIC Stream" -autoexit > /dev/null 2>&1

# 退出处理
echo ""
echo "[INFO] 正在停止所有服务..."
kill $SERVER_PID $FFMPEG_PID 2>/dev/null || true
echo "[INFO] 演示结束。"

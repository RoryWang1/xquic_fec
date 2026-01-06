# XQUIC 视频流项目开发总结

## 1. 项目概述
本项目实现了基于 XQUIC (QUIC 协议) 的实时视频流传输系统。
- **Server**: 从命名管道 (FIFO) 读取视频数据 (H.264/MPEG-TS)，通过 QUIC Stream 发送。
- **Client**: 接收 QUIC Stream 数据，通过标准输出 (Stdout) 管道传输给播放器 (FFplay)。

## 2. 遇到的问题与解决方案

在开发过程中，我们主要解决了以下三个核心问题：

### 问题一：客户端启动即崩溃 (Segmentation Fault)

*   **错误现象**：
    客户端启动连接后，在握手阶段立即发生崩溃（Segmentation fault: 11）。使用 Signal Handler 捕捉到的堆栈跟踪指向 `xqc_process_new_token_frame`。

*   **原因分析**：
    XQUIC 客户端在处理服务器发送的 `NEW_TOKEN` 或 `SESSION_TICKET` 帧时，会尝试调用用户注册的回调函数。在我们的初始实现中，缺少了 `save_token`, `save_session_cb`, `save_tp_cb` 这三个关键回调。当库函数尝试调用这些为 `NULL` 的函数指针时，导致了崩溃。

*   **解决方法**：
    在 `src/camera_client.c` 中实现了这三个回调函数（即使只是简单的日志打印），并在创建引擎时的 `xqc_transport_callbacks_t` 结构体中进行了正确注册。

    ```c
    xqc_transport_callbacks_t tcbs = {
        .write_socket = client_write_socket,
        .save_token = client_save_token,          // 修复：注册 save_token
        .save_session_cb = client_save_session_cb,  // 修复：注册 save_session_cb
        .save_tp_cb = client_save_tp_cb            // 修复：注册 save_tp_cb
    };
    ```

### 问题二：视频数据无法传输 (Broken Pipe / Zero Reads)

*   **错误现象**：
    连接建立成功，客户端、服务端均显示正常运行，但视频画面不显示。FFmpeg 报错 "Broken pipe"，服务端日志虽显示已启用 FIFO 读取，但实际读取到的字节数始终为 0。

*   **原因分析**：
    1.  **UDP/Pipe EOF 误判**：服务端最初以 `O_RDONLY | O_NONBLOCK` 模式打开 FIFO。在 Linux/macOS 中，如果 FIFO 的写入端（FFmpeg）断开或未及时连接，读取端会立即收到 EOF (0 bytes)。我们的代码将 EOF 视为流结束，从而停止了监听。
    2.  **macOS kqueue 兼容性**：在 macOS 环境下，libevent (基于 kqueue) 对命名管道（FIFO）的边缘触发或电平触发支持存在不稳定性。即使管道中有数据，`EV_READ` 事件也可能不会被触发。

*   **解决方法**：
    1.  **O_RDWR 模式**：修改服务端代码，以 `O_RDWR` 模式打开 FIFO。这使得服务端自身持有一个写入句柄，从而保证管道永远不会进入 EOF 状态（引用计数 > 0），即使 FFmpeg 频繁重启也能保持连接。
    2.  **定时器轮询 (Timer Poller)**：作为双重保险，在服务端引入了一个 5ms 的定时器事件。即使系统的 IO 事件通知失效，定时器也会确保持续尝试从 FIFO 读取数据。

### 问题三：视频流数据损坏 (Stdout Pollution)

*   **错误现象**：
    FFplay 无法解析视频流，或者画面出现大量花屏、报错。

*   **原因分析**：
    客户端代码中混用了 `printf`（输出到标准输出）来打印调试信息。由于我们将标准输出（Stdout）作为视频数据的传输管道（Pipe），这些文本日志混入了二进制视频数据中，破坏了 MPEG-TS 的数据结构。

*   **解决方法**：
    全面审查代码，将 `src/camera_client.c` 中所有的日志输出严格重定向到 `Stderr`（标准错误输出），确保 Stdout 通道纯净。

## 3. 运行方法

已提供一键启动脚本 `run_camera_demo.sh`，直接运行即可：

```bash
./run_camera_demo.sh
```

手动运行步骤：
1. 启动 Server: `./build/camera_server`
2. 启动 FFmpeg 推流: `ffmpeg -re -f lavfi -i testsrc ... -f mpegts /tmp/camera_fifo`
3. 启动 Client 播放: `./build/camera_client | ffplay -i -`

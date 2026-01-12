# XQUIC性能测试 - 当前阻塞问题

## 测试状态

**基本功能**：✅ 已修复
- 网络模拟端口配置（8443→4433）
- UDP解码器时序问题
- 窗口自动关闭功能

**当前瓶颈**：❌ XQUIC流在网络压力下无法显示

## 问题详情

### Baseline场景（无网络模拟）
- UDP流：正常 ✅
- XQUIC流：正常（355帧，21 FPS）✅

### Packet Loss 5%场景（50ms延迟 + 5%丢包）
- UDP流：正常（虽然有解码错误）✅
- XQUIC流：**完全无画面** ❌

## 错误信息

```
[out#0/rawvideo @ 0x15bf06500] Error opening output /tmp/raw_right: Interrupted system call
Error opening output file /tmp/raw_right.
Error opening output files: Interrupted system call
```

## 已尝试的修复

1. **增加FFmpeg等待时间**（3秒→10秒）- 无效
2. **调整启动顺序**（先启动dual_player）- 无效  
3. **增加thread_queue_size**和时间戳处理 - 无效

## 根本原因猜测

在5%丢包+50ms延迟环境下：
- XQUIC握手成功但数据传输极慢
- FFmpeg等待数据超时后尝试打开输出FIFO
- 此时收到中断信号（可能是超时或其他清理信号）

## 下一步选项

### 选项1：降低难度测试
- 测试2%丢包场景，看XQUIC是否能工作
- 逐步增加难度找到阈值

### 选项2：深度调试
- 修改camera_client，立即flush stdout
- 增加XQUIC的重传参数
- 调整超时配置

### 选项3：改变实验设计
- 使用真实网络环境（两台机器）
- 或使用tc (traffic control) 替代dummynet
- 或接受localhost模拟的局限性

### 选项4：检查XQUIC配置
可能需要调整`xqc_config_t`中的参数：
- `cfg_log_level` - 增加日志详细程度  
- congestion control参数
- 重传超时参数

## 技术细节

**XQUIC Client日志显示**：
- 连接建立成功
- 握手完成
- **但stream_read只发生了1次**（正常应该持续读取）

**这表明**：真正的问题可能在server端 - 在网络延迟下**server没有持续发送数据**！

## 建议

需要用户决定下一步行动：
1. 接受5%丢包场景下XQUIC无法工作？
2. 继续深度调试server端？
3. 调整实验设计？

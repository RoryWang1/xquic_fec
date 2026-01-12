# XQUIC实验快速入门

## 前提条件

1. **网络模拟工具（macOS）**
   
   macOS已经内置了`pfctl`和`dnctl`，可以直接使用。不需要额外安装。

2. **编译项目**
   
   确保项目已成功编译：
   ```bash
   cd /Users/rory/work/my_xquic_project
   mkdir -p build && cd build
   cmake ..
   make -j4
   ```

3. **权限**
   
   网络模拟需要sudo权限。脚本会在需要时提示输入密码。

## 运行单个实验

### 1. 基线测试（良好网络）

```bash
cd experiments/scripts
./run_experiment.sh baseline 60
```

这会运行60秒的基线测试，不施加任何网络限制。

### 2. 高延迟测试

```bash
./run_experiment.sh high_latency_50ms 60
```

模拟50ms延迟，观察XQUIC的连接建立优势。

### 3. 丢包测试

```bash
# 轻度丢包 (2%)
./run_experiment.sh packet_loss_2pct 60

# 中度丢包 (5%) - XQUIC优势开始明显
./run_experiment.sh packet_loss_5pct 60

# 重度丢包 (10%) - XQUIC优势最明显
./run_experiment.sh packet_loss_10pct 60
```

### 4. 带宽受限测试

```bash
./run_experiment.sh limited_bandwidth 60
```

模拟2Mbps带宽限制。

### 5. 综合恶劣网络

```bash
./run_experiment.sh challenging 60
```

同时模拟高延迟、丢包和带宽限制。

## 批量运行所有实验

```bash
cd experiments/scripts
./batch_test.sh all 60
```

这会自动运行所有7个场景，每个60秒。总共约7分钟。

## 查看结果

### 实时观察

实验运行时，会打开一个SDL2窗口显示对比画面：
- **左侧**: 原始UDP流
- **右侧**: XQUIC流

直观观察两者的延迟、流畅度差异。

### 查看日志

每个实验的结果保存在：
```
experiments/results/[scenario_name]/[timestamp]/
├── logs/
│   ├── server.log          # 服务器日志
│   ├── client.log          # XQUIC客户端日志
│   ├── decoder_left.log    # UDP解码器日志
│   ├── decoder_right.log   # XQUIC解码器日志
│   └── ffmpeg_source.log   # 视频源日志
├── metrics.json            # 基础指标
├── network_config.profile  # 网络配置
└── summary.txt            # 实验摘要
```

### 分析结果

```bash
cd experiments/tools

# 分析单个实验
./analyze_results.py ../results/packet_loss_5pct/

# 分析批量实验
./analyze_results.py ../results/batch_20260107_100000/
```

分析工具会生成：
- 文本报告：`analysis_report.txt`
- JSON数据：`analysis_report.json`

## 典型实验流程

### 流程1: 快速验证XQUIC优势

```bash
cd experiments/scripts

# 1. 基线测试（30秒即可）
./run_experiment.sh baseline 30

# 2. 中度丢包测试（XQUIC优势明显）
./run_experiment.sh packet_loss_5pct 60

# 3. 对比分析
cd ../tools
./analyze_results.py ../results/
```

### 流程2: 完整性能评估

```bash
cd experiments/scripts

# 运行所有场景（每个60秒）
./batch_test.sh all 60

# 等待完成后分析
cd ../tools
./analyze_results.py ../results/batch_*/
```

## 预期现象

### 基线测试
- UDP和XQUIC表现相似
- 两者FPS都接近30
- 延迟都很低

### 丢包测试 (5-10%)
- **UDP流**: 会出现明显卡顿、花屏
- **XQUIC流**: 更流畅，因为多路复用避免了队头阻塞
- FPS差异明显

### 高延迟测试
- **连接建立**: XQUIC更快（0-RTT）
- **稳定传输**: 两者相似

### 带宽受限测试
- **码率稳定性**: XQUIC的BBR拥塞控制表现更好
- **适应性**: XQUIC能更好地适应带宽变化

## 故障排除

### 1. 网络模拟不生效

**症状**: 运行测试后感觉没有延迟/丢包

**解决**:
```bash
# 检查dnctl配置
sudo dnctl show

# 检查pf规则
sudo pfctl -s rules | grep dummynet

# 手动清理并重试
sudo ./cleanup_network.sh
sudo ./setup_network.sh ../network_profiles/packet_loss_5pct.profile
```

### 2. 服务器启动失败

**症状**: `Error: Server failed to start`

**解决**:
```bash
# 检查端口占用
lsof -i :8443

# 杀死占用进程
pkill -f camera_server

# 检查证书文件
ls -la ../certs/
```

### 3. 摄像头权限问题

**症状**: FFmpeg无法访问摄像头

**解决**:
- macOS系统设置 -> 隐私与安全性 -> 摄像头
- 允许Terminal访问摄像头

### 4. 清理网络配置

实验被中断时，网络配置可能未自动清理：

```bash
sudo ./scripts/cleanup_network.sh
```

## 自定义实验

### 创建新的网络配置

复制并修改现有的profile文件：

```bash
cd experiments/network_profiles
cp packet_loss_5pct.profile my_custom.profile
```

编辑`my_custom.profile`：
```bash
LATENCY_MS=100          # 延迟
PACKET_LOSS_PCT=3       # 丢包率
BANDWIDTH_MBPS=5        # 带宽限制
JITTER_MS=20            # 抖动
DESCRIPTION="My custom network scenario"
```

运行：
```bash
cd ../scripts
./run_experiment.sh my_custom 60
```

## 下一步

1. **收集数据**: 运行批量测试收集完整数据
2. **分析结果**: 使用分析工具生成报告
3. **可视化**: 可以进一步开发绘图工具（plot_comparison.py）
4. **论文撰写**: 使用实验数据支撑XQUIC优势论述

## 注意事项

⚠️ **资源占用**: 同时运行编码、解码、传输会占用大量CPU，确保电脑性能足够

⚠️ **测试时长**: 建议每个场景至少60秒，以获得稳定的统计数据

⚠️ **网络隔离**: 测试期间尽量不要进行其他网络活动，避免干扰

⚠️ **重复测试**: 建议每个场景重复3-5次，取平均值，提高可信度

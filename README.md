# XQUIC Video Streaming Project

基于XQUIC（阿里开源QUIC实现）的实时视频传输项目，包含完整的性能对比实验框架。

## 项目概述

本项目实现了一个基于XQUIC协议的实时视频传输系统，用于验证QUIC协议在视频传输场景下相比传统UDP的性能优势。

### 核心功能

- ✅ **Camera Server**: XQUIC视频服务器，从FIFO读取MPEG-TS流并通过QUIC传输
- ✅ **Camera Client**: XQUIC客户端，接收视频流并输出到标准输出
- ✅ **Dual Player**: SDL2双屏播放器，并排对比UDP和XQUIC流
- ✅ **Performance Testing**: 完整的性能测试和对比实验框架

### 项目特点

- 🔐 基于TLS 1.3的安全传输
- ⚡ QUIC协议的0-RTT连接建立
- 🔄 多路复用避免队头阻塞
- 📊 完整的性能测试和分析工具
- 🎥 实时视频流对比演示

## 快速开始

### 1. 编译项目

```bash
# 初始化并编译
mkdir -p build && cd build
cmake ..
make -j4
```

### 2. 运行演示

#### 简单演示（XQUIC流）

```bash
./run_camera_demo.sh
```

这会启动server、client和播放器，显示XQUIC视频流。

#### 对比演示（UDP vs XQUIC）

```bash
./compare_camera_demo.sh
```

并排显示：
- 左侧：传统UDP流
- 右侧：XQUIC流

### 3. 性能测试 ⭐

运行完整的性能对比实验：

```bash
cd experiments/scripts
./quick_demo.sh  # 快速演示（30秒）
# 或
./batch_test.sh all 60  # 完整测试（约10分钟）
```

详见 **[experiments/](./experiments/)** 目录。

## 项目结构

```
my_xquic_project/
├── src/                      # 源代码
│   ├── camera_server.c       # XQUIC视频服务器
│   ├── camera_client.c       # XQUIC客户端
│   └── dual_player.c         # SDL2双屏播放器
├── third_party/xquic/        # XQUIC库（submodule）
├── experiments/ ⭐            # 性能测试框架
│   ├── README.md             # 实验框架说明
│   ├── QUICKSTART.md         # 快速入门
│   ├── EXPERIMENT_DESIGN.md  # 实验设计文档
│   ├── network_profiles/     # 网络场景配置
│   ├── scripts/              # 测试脚本
│   └── tools/                # 分析工具
├── study_note/               # 学习笔记
│   └── README.md             # XQUIC深入学习笔记
├── run_camera_demo.sh        # 运行XQUIC演示
├── compare_camera_demo.sh    # 运行对比演示
└── CMakeLists.txt            # 构建配置
```

## 性能实验

### 实验场景

| 场景 | 网络条件 | XQUIC优势 |
|------|---------|----------|
| 良好网络 | 0ms延迟, 0%丢包 | 基线 |
| 高延迟 | 50ms延迟 | 连接建立快 |
| 轻度丢包 | 2%丢包 | 小幅提升 |
| **中度丢包** | **5%丢包** | **明显提升** ⭐ |
| **重度丢包** | **10%丢包** | **显著提升** ⭐⭐ |
| 带宽受限 | 2Mbps | 码率稳定 |
| 综合恶劣 | 多种问题 | **最大优势** ⭐⭐⭐ |

### 运行实验

```bash
# 进入实验目录
cd experiments

# 阅读实验说明
cat README.md

# 快速演示（推荐）
./scripts/quick_demo.sh

# 批量测试所有场景
./scripts/batch_test.sh all 60

# 分析结果
./tools/analyze_results.py results/
```

详细说明见 **[experiments/README.md](./experiments/README.md)**

## 技术栈

- **协议**: XQUIC (QUIC + HTTP/3)
- **视频编码**: H.264 (libx264)
- **封装格式**: MPEG-TS
- **TLS**: BoringSSL
- **播放**: SDL2
- **视频处理**: FFmpeg

## 系统要求

- **操作系统**: macOS (已测试)
- **编译器**: Clang/GCC with C11 support
- **依赖**:
  - CMake >= 3.10
  - FFmpeg (with libx264)
  - SDL2
  - BoringSSL (included in XQUIC)
  - Python 3 (for analysis tools)

## 学习资源

- **[study_note/README.md](./study_note/README.md)** - XQUIC协议深入学习笔记
- **[experiments/EXPERIMENT_DESIGN.md](./experiments/EXPERIMENT_DESIGN.md)** - 完整的实验设计文档
- [XQUIC官方文档](https://github.com/alibaba/xquic)
- [IETF QUIC工作组](https://quicwg.org/)

## 使用场景

### 1. 学术研究
- 提供完整的实验框架和数据收集工具
- 量化QUIC协议在视频传输中的优势
- 支持论文撰写和数据分析

### 2. 技术学习
- 理解QUIC协议的实际应用
- 学习实时视频传输技术
- 掌握性能测试方法

### 3. 技术决策
- 评估QUIC在实际场景中的表现
- 对比不同传输协议的优劣
- 为技术选型提供数据支持

## 常见问题

### Q: 如何快速看到XQUIC的优势？

A: 运行快速演示脚本：
```bash
cd experiments/scripts
./quick_demo.sh
```
这会在5%丢包环境下对比UDP和XQUIC，你会明显看到右侧（XQUIC）更流畅。

### Q: 测试需要多长时间？

A: 
- 快速演示：30秒
- 单个场景：1-2分钟
- 完整测试：约10分钟（7个场景 × 60秒）

### Q: 如何分析结果？

A: 使用提供的分析工具：
```bash
cd experiments/tools
./analyze_results.py ../results/
```

### Q: 可以在生产环境使用吗？

A: 这是一个实验/演示项目，主要用于学习和研究。生产使用需要进一步优化和测试。

## 故障排除

常见问题和解决方案见：
- [experiments/QUICKSTART.md](./experiments/QUICKSTART.md) - 故障排除部分
- 各脚本的注释

## 开发笔记

- **development_summary.md**: 开发过程记录和关键决策

## License

本项目基于 Apache License 2.0
XQUIC库使用 Apache License 2.0

## 致谢

- [XQUIC](https://github.com/alibaba/xquic) - 阿里开源的QUIC实现
- FFmpeg - 视频处理工具
- SDL2 - 视频渲染库

---

**开始试用**: `cd experiments && ./scripts/quick_demo.sh`

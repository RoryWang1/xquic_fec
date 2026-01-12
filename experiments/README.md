# XQUIC性能实验指南

## 📋 概述

本目录包含完整的XQUIC vs UDP视频传输性能对比实验框架。通过模拟各种网络环境（高延迟、丢包、带宽受限等），量化验证XQUIC在视频传输场景下的优势。

## 🚀 快速开始

### 1. 快速演示（推荐第一次使用）

30秒快速演示，直观看到XQUIC的优势：

```bash
cd experiments/scripts
./quick_demo.sh
```

这会运行一个5%丢包场景的测试，你会看到：
- 左侧（UDP）：卡顿、花屏
- 右侧（XQUIC）：流畅播放

### 2. 完整实验

运行所有7个场景（约10分钟）：

```bash
cd experiments/scripts
./batch_test.sh all 60
```

### 3. 分析结果

```bash
cd experiments/tools
./analyze_results.py ../results/batch_*/
```

## 📚 文档

- **[QUICKSTART.md](./QUICKSTART.md)** - 详细使用说明和故障排除
- **[EXPERIMENT_DESIGN.md](./EXPERIMENT_DESIGN.md)** - 完整的实验设计和理论背景
- **[README.md](./README.md)** - 实验框架总览

## 🧪 实验场景

| 场景 | 网络条件 | XQUIC预期优势 |
|------|---------|--------------|
| baseline | 理想网络 | 基线（性能相当） |
| high_latency_50ms | 50ms延迟 | 连接建立快 |
| packet_loss_2pct | 2%丢包 | 轻度提升 |
| packet_loss_5pct | 5%丢包 | **明显提升** ⭐ |
| packet_loss_10pct | 10%丢包 | **显著提升** ⭐⭐ |
| limited_bandwidth | 2Mbps限制 | 码率稳定 |
| challenging | 综合恶劣 | **最大提升** ⭐⭐⭐ |

## 📊 预期结果示例

```
场景                  UDP FPS    XQUIC FPS   提升
baseline             30.0       30.0        0%
packet_loss_5pct     25.3       28.7        +13%
packet_loss_10pct    18.2       26.5        +46%
challenging          20.5       27.3        +33%
```

## 🛠 目录结构

```
experiments/
├── README.md                    # 总览
├── QUICKSTART.md                # 快速入门
├── EXPERIMENT_DESIGN.md         # 实验设计文档
├── network_profiles/            # 7个网络配置文件
│   ├── baseline.profile
│   ├── packet_loss_5pct.profile
│   └── ...
├── scripts/                     # 执行脚本
│   ├── quick_demo.sh           # 快速演示 ⭐
│   ├── run_experiment.sh       # 单个实验
│   ├── batch_test.sh           # 批量测试
│   ├── setup_network.sh        # 网络模拟
│   └── cleanup_network.sh      # 清理
└── tools/                       # 分析工具
    └── analyze_results.py      # 结果分析 ⭐
```

## ⚠️ 注意事项

1. **需要sudo权限**: 网络模拟使用dnctl/pfctl
2. **仅macOS**: 当前脚本针对macOS优化
3. **摄像头权限**: 确保Terminal有摄像头访问权限
4. **资源占用**: 测试期间避免其他高负载任务

## 🎯 适用场景

此实验框架适合：
- 📖 **学术研究**: 论文实验数据
- 🎓 **学习理解**: QUIC协议优势验证
- 💼 **技术决策**: 评估QUIC在视频传输中的价值
- 📊 **性能对比**: 量化不同协议的表现

## 💡 下一步

1. 运行 `quick_demo.sh` 看看效果
2. 阅读 `QUICKSTART.md` 了解详细用法
3. 运行批量测试收集完整数据
4. 使用分析工具生成报告

## 🤝 反馈

如有问题，查看各脚本的注释或 `QUICKSTART.md` 中的故障排除部分。

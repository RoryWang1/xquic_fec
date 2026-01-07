#!/bin/bash
# quick_demo.sh - 快速演示XQUIC优势（5%丢包场景，30秒）
# 这是一个快速演示脚本，用于验证环境配置和快速看到XQUIC的优势

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "========================================="
echo "   XQUIC Quick Demo"
echo "========================================="
echo "This will run a 30-second test with 5% packet loss"
echo "to quickly demonstrate XQUIC's advantage."
echo ""
echo "You should see:"
echo "  - Left (UDP): More stuttering and artifacts"
echo "  - Right (XQUIC): Smoother playback"
echo ""
echo "Press Enter to start, or Ctrl+C to cancel..."
read

# 运行实验
"${SCRIPT_DIR}/run_experiment.sh" packet_loss_5pct 30

echo ""
echo "========================================="
echo "   Demo Complete!"
echo "========================================="
echo ""
echo "Next steps:"
echo "  1. Review the visual difference you observed"
echo "  2. Run full experiment: ./batch_test.sh all 60"
echo "  3. Analyze results: ./tools/analyze_results.py results/"
echo ""

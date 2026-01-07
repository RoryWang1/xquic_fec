#!/bin/bash
# batch_test.sh - 批量运行多个实验场景
# Usage: ./batch_test.sh [all|list] <duration_per_test>

set -e

MODE="${1:-all}"
DURATION="${2:-60}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPERIMENT_DIR="$(dirname "$SCRIPT_DIR")"

# 定义测试场景顺序
SCENARIOS=(
    "baseline"
    "high_latency_50ms"
    "packet_loss_2pct"
    "packet_loss_5pct"
    "packet_loss_10pct"
    "limited_bandwidth"
    "challenging"
)

if [[ "$MODE" == "list" ]]; then
    echo "Available scenarios:"
    for scenario in "${SCENARIOS[@]}"; do
        echo "  - $scenario"
    done
    exit 0
fi

if [[ "$MODE" != "all" ]]; then
    echo "Usage: $0 [all|list] <duration_per_test>"
    echo "  all  - Run all scenarios sequentially"
    echo "  list - List available scenarios"
    exit 1
fi

echo "========================================="
echo "   XQUIC Batch Experiment"
echo "========================================="
echo "Running ${#SCENARIOS[@]} scenarios"
echo "Duration per test: ${DURATION}s"
echo "Total estimated time: $((${#SCENARIOS[@]} * DURATION / 60)) minutes"
echo "========================================="
echo ""

BATCH_START=$(date +%s)
BATCH_RESULTS_DIR="${EXPERIMENT_DIR}/results/batch_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BATCH_RESULTS_DIR"

# 运行所有场景
COMPLETED=0
FAILED=0

for scenario in "${SCENARIOS[@]}"; do
    echo ""
    echo "========================================="
    echo "[$((COMPLETED + FAILED + 1))/${#SCENARIOS[@]}] Running: $scenario"
    echo "========================================="
    
    if "${SCRIPT_DIR}/run_experiment.sh" "$scenario" "$DURATION"; then
        echo "[OK] $scenario completed successfully"
        COMPLETED=$((COMPLETED + 1))
        
        # 把结果链接到batch目录
        LATEST_RESULT=$(ls -td "${EXPERIMENT_DIR}/results/${scenario}"/* | head -1)
        ln -s "$LATEST_RESULT" "${BATCH_RESULTS_DIR}/${scenario}"
    else
        echo "[FAILED] $scenario failed!"
        FAILED=$((FAILED + 1))
    fi
    
    # 等待一段时间让系统稳定
    if [[ $((COMPLETED + FAILED)) -lt ${#SCENARIOS[@]} ]]; then
        echo "Waiting 10 seconds before next test..."
        sleep 10
    fi
done

BATCH_END=$(date +%s)
BATCH_DURATION=$((BATCH_END - BATCH_START))

echo ""
echo "========================================="
echo "   BATCH EXPERIMENT COMPLETE"
echo "========================================="
echo "Completed: $COMPLETED"
echo "Failed: $FAILED"
echo "Total time: $((BATCH_DURATION / 60)) minutes"
echo ""
echo "Results directory: $BATCH_RESULTS_DIR"
echo ""
echo "Next: Run analysis tool"
echo "  ./tools/analyze_results.py $BATCH_RESULTS_DIR"
echo "========================================="

# 创建batch摘要
cat > "${BATCH_RESULTS_DIR}/batch_summary.txt" << EOF
XQUIC Batch Experiment Summary
===============================

Start: $(date -d "@$BATCH_START" 2>/dev/null || date -r "$BATCH_START")
End:   $(date -d "@$BATCH_END" 2>/dev/null || date -r "$BATCH_END")
Duration: $((BATCH_DURATION / 60)) minutes

Scenarios Run: ${#SCENARIOS[@]}
Completed: $COMPLETED
Failed: $FAILED

Individual Results:
$(for scenario in "${SCENARIOS[@]}"; do
    if [[ -L "${BATCH_RESULTS_DIR}/${scenario}" ]]; then
        echo "  [OK] $scenario"
    else
        echo "  [FAIL] $scenario"
    fi
done)

Results Location: $BATCH_RESULTS_DIR
===============================
EOF

cat "${BATCH_RESULTS_DIR}/batch_summary.txt"

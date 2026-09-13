#!/bin/bash
# 从批量测试结果中挑选代表性图片
# 用法：在容器内执行 bash select_samples.sh

RESULTS_DIR="/workspace/infer/results"
OUTPUT_DIR="/workspace/samples"

# 每张图的目标数（来自批量测试输出）
# 格式: 文件名:目标数
declare -A COUNTS=(
    ["test_01.jpg"]=2
    ["test_02.jpg"]=1
    ["test_03.jpg"]=12
    ["test_04.jpg"]=10
    ["test_05.jpg"]=1
    ["test_06.jpg"]=16
    ["test_07.jpg"]=7
    ["test_08.jpg"]=11
    ["test_09.jpg"]=8
    ["test_10.jpg"]=81
    ["test_11.jpg"]=58
    ["test_12.jpg"]=15
    ["test_13.jpg"]=6
    ["test_14.jpg"]=27
    ["test_15.jpg"]=11
    ["test_16.jpg"]=13
    ["test_17.jpg"]=5
    ["test_18.jpg"]=12
    ["test_19.jpg"]=15
    ["test_20.jpg"]=28
    ["test_21.jpg"]=12
    ["test_22.jpg"]=8
    ["test_23.jpg"]=3
    ["test_24.jpg"]=9
    ["test_25.jpg"]=8
    ["test_26.jpg"]=12
    ["test_27.jpg"]=13
    ["test_28.jpg"]=1
    ["test_29.jpg"]=18
    ["test_30.jpg"]=1
)

mkdir -p "$OUTPUT_DIR"

echo "========================================"
echo "  挑选代表性结果图"
echo "========================================"
echo ""

# 1. 目标最多的图（最密集场景）
MAX_FILE=""
MAX_COUNT=0
for f in "${!COUNTS[@]}"; do
    if [ "${COUNTS[$f]}" -gt "$MAX_COUNT" ]; then
        MAX_COUNT="${COUNTS[$f]}"
        MAX_FILE="$f"
    fi
done
echo "[1] 目标最多: $MAX_FILE (${MAX_COUNT}个目标) - 密集车流场景"
cp "$RESULTS_DIR/${MAX_FILE%.jpg}_result.jpg" "$OUTPUT_DIR/01_最密集_${MAX_COUNT}目标_${MAX_FILE}" 2>/dev/null || \
cp "$RESULTS_DIR/$MAX_FILE" "$OUTPUT_DIR/01_最密集_${MAX_COUNT}目标_${MAX_FILE}" 2>/dev/null

# 2. 目标最少的图（简单场景）
MIN_FILE=""
MIN_COUNT=999
for f in "${!COUNTS[@]}"; do
    if [ "${COUNTS[$f]}" -lt "$MIN_COUNT" ]; then
        MIN_COUNT="${COUNTS[$f]}"
        MIN_FILE="$f"
    fi
done
echo "[2] 目标最少: $MIN_FILE (${MIN_COUNT}个目标) - 简单场景"
cp "$RESULTS_DIR/${MIN_FILE%.jpg}_result.jpg" "$OUTPUT_DIR/02_最简单_${MIN_COUNT}目标_${MIN_FILE}" 2>/dev/null || \
cp "$RESULTS_DIR/$MIN_FILE" "$OUTPUT_DIR/02_最简单_${MIN_COUNT}目标_${MIN_FILE}" 2>/dev/null

# 3. 目标数中等的图（典型场景）
MID_FILE="test_03.jpg"  # 12个目标，多车+多人
echo "[3] 典型场景: $MID_FILE (${COUNTS[$MID_FILE]}个目标) - 多车+行人混合"
cp "$RESULTS_DIR/${MID_FILE%.jpg}_result.jpg" "$OUTPUT_DIR/03_典型_${COUNTS[$MID_FILE]}目标_${MID_FILE}" 2>/dev/null || \
cp "$RESULTS_DIR/$MID_FILE" "$OUTPUT_DIR/03_典型_${COUNTS[$MID_FILE]}目标_${MID_FILE}" 2>/dev/null

# 4. 含交通灯的图（test_04有traffic light）
TL_FILE="test_04.jpg"
echo "[4] 多类别: $TL_FILE (${COUNTS[$TL_FILE]}个目标) - 含交通灯检测"
cp "$RESULTS_DIR/${TL_FILE%.jpg}_result.jpg" "$OUTPUT_DIR/04_多类别_含交通灯_${TL_FILE}" 2>/dev/null || \
cp "$RESULTS_DIR/$TL_FILE" "$OUTPUT_DIR/04_多类别_含交通灯_${TL_FILE}" 2>/dev/null

# 5. 卡车为主的图（test_13全是truck）
TRUCK_FILE="test_13.jpg"
echo "[5] 卡车场景: $TRUCK_FILE (${COUNTS[$TRUCK_FILE]}个目标) - 卡车为主"
cp "$RESULTS_DIR/${TRUCK_FILE%.jpg}_result.jpg" "$OUTPUT_DIR/05_卡车场景_${TRUCK_FILE}" 2>/dev/null || \
cp "$RESULTS_DIR/$TRUCK_FILE" "$OUTPUT_DIR/05_卡车场景_${TRUCK_FILE}" 2>/dev/null

echo ""
echo "========================================"
echo "  完成！已挑选 5 张代表性图片"
echo "========================================"
echo ""
echo "输出目录: $OUTPUT_DIR"
ls -la "$OUTPUT_DIR"
echo ""
echo "这些文件在 Ubuntu 的 ~/workspace/samples/ 目录下"
echo "可以直接拖拽到 Windows 的 GitHub 项目 results/samples/ 目录"

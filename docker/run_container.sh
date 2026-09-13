#!/bin/bash
# 地平线 J5 工具链容器启动脚本
# 用法：./run_container.sh

CONTAINER_NAME="horizon_j5"
IMAGE="openexplorer/ai_toolchain_ubuntu_20_j5_cpu:v1.1.77-py38"
WORKSPACE="${HOME}/workspace"

# 检查容器是否已存在
if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "容器 ${CONTAINER_NAME} 已存在，启动并进入..."
    docker start ${CONTAINER_NAME}
    docker exec -it ${CONTAINER_NAME} /bin/bash
else
    echo "创建新容器 ${CONTAINER_NAME}..."
    docker run -it --name ${CONTAINER_NAME} \
        --privileged \
        -v ${WORKSPACE}:/workspace \
        -w /workspace \
        ${IMAGE} /bin/bash
fi

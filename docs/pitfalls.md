# 踩坑记录

> 记录从环境搭建到部署验证过程中遇到的 18 个实际问题。
> 每个问题包含：现象 → 原因 → 解决方案 → 经验总结。

---

## 目录

1. [容器管理类](#1-容器管理类)
2. [环境依赖类](#2-环境依赖类)
3. [模型转换类](#3-模型转换类)
4. [推理开发类](#4-推理开发类)
5. [CAN 通信类](#5-can-通信类)
6. [结果调优类](#6-结果调优类)

---

## 1. 容器管理类

### 坑1：容器关闭后如何恢复

**现象**：不小心关闭了终端或容器，之前的工作环境丢失。

**原因**：Docker 容器停止后不会自动重启，需要手动启动并重新进入。

**解决方案**：
```bash
# 查看所有容器（包括已停止的）
docker ps -a

# 启动已停止的容器
docker start horizon_j5

# 重新进入容器（不要用 docker run，那会创建新容器）
docker exec -it horizon_j5 /bin/bash
```

**经验**：`docker run` 是创建新容器，`docker start` + `docker exec` 是恢复旧容器。容器内的文件修改会保留，但 vcan 等网络接口需要重新创建。

---

### 坑2：终端对话消失后找不到之前的输出

**现象**：终端关闭后，之前的编译输出、推理结果都看不到了。

**原因**：终端输出是临时的，不会持久化。

**解决方案**：
```bash
# 重要命令用 tee 同时输出到文件
./infer_demo 2>&1 | tee result.txt

# 后台运行并记录日志
nohup ./batch_test > batch_log.txt 2>&1 &
```

**经验**：养成用 `tee` 记录关键输出的习惯，尤其是批量测试和编译过程。

---

## 2. 环境依赖类

### 坑3：`./debug_output: No such file or directory`

**现象**：编译成功后运行可执行文件，提示找不到文件。

**原因**：可执行文件名不匹配，或当前目录不对。

**解决方案**：
```bash
# 查看当前目录下的可执行文件
ls -la *.o infer_demo batch_test 2>/dev/null
ls -la build/

# 确认 CMakeLists.txt 中的 target 名
grep "add_executable" CMakeLists.txt
```

**经验**：CMake 的 `add_executable(target_name source.cpp)` 决定了输出文件名，运行时要用 `./target_name`。

---

### 坑4：`grep` 搜索结构体定义无输出

**现象**：
```bash
grep -A 30 "typedef struct.*hbDNNTensorProperties\|struct hbDNNTensorProperties" hb_dnn.h
# 无任何输出
```

**原因**：地平线头文件中结构体定义是 `typedef struct { ... } hbDNNTensorProperties;` 格式，结构体名在末尾，grep 模式匹配不到。

**解决方案**：
```bash
# 方法1：搜索类型名
grep -n "hbDNNTensorProperties" hb_dnn.h

# 方法2：搜索 struct 关键字
grep -n "typedef struct" hb_dnn.h

# 方法3：直接看行号范围
sed -n '95,115p' hb_dnn.h
```

**经验**：读头文件先 `grep -n "关键字"` 拿到行号，再 `sed -n '起始,结束p'` 看上下文，比复杂的 grep 模式更可靠。

---

### 坑5：新容器中 CMake 找不到 OpenCV

**现象**：重新创建容器后编译报错：
```
Could not find a package configuration file provided by "OpenCV"
```

**原因**：新容器是干净的镜像，没有安装 OpenCV 开发包。之前的容器里装过但新容器没有。

**解决方案**：
```bash
apt update
apt install -y libopencv-dev
# 或
apt install -y opencv-python
```

**经验**：Docker 容器的安装不会持久化到镜像。如果频繁重建容器，写一个 `setup.sh` 脚本统一安装依赖，或者用 `docker commit` 保存当前容器为新镜像。

---

### 坑6：`modprobe: command not found`

**现象**：在容器内执行 `modprobe vcan` 报错。

**原因**：容器是精简的 Ubuntu 镜像，没有安装 `kmod` 包（提供 modprobe）。而且 vcan 是内核模块，容器内通常无法直接 modprobe。

**解决方案**：容器内不需要 modprobe，直接用 `ip link` 创建 vcan 接口（前提是容器以 `--privileged` 启动）：
```bash
ip link add dev vcan0 type vcan
ip link set up vcan0
```

**经验**：虚拟 CAN（vcan）不需要内核模块加载，`ip link` 命令即可创建。真实 CAN 硬件才需要 modprobe 对应驱动。

---

### 坑7：`ip` 命令不存在

**现象**：`which ip` 无输出，`ip link` 报错。

**原因**：精简镜像没有安装 `iproute2` 包。

**解决方案**：
```bash
apt update && apt install -y iproute2 can-utils
```

**经验**：`iproute2` 提供 `ip` 命令，`can-utils` 提供 `candump`/`cansend` 等 CAN 工具，两个一起装。

---

### 坑8：容器非特权模式无法创建 CAN 接口

**现象**：`ip link add dev vcan0 type vcan` 报权限错误。

**原因**：容器默认没有网络管理权限，需要 `--privileged` 或至少 `--cap-add=NET_ADMIN`。

**解决方案**：
```bash
# 停止并删除旧容器
docker stop horizon_j5
docker rm horizon_j5

# 以特权模式重新创建
docker run -it --name horizon_j5 --privileged \
  -v ~/workspace:/workspace \
  openexplorer/ai_toolchain_ubuntu_20_j5_cpu:v1.1.77-py38 /bin/bash
```

**经验**：涉及网络接口、设备访问的操作，容器必须以特权模式启动。如果不想用全特权，可以用 `--cap-add=NET_ADMIN --device=/dev/net/tun` 更精细地授权。

---

## 3. 模型转换类

### 坑9：工具链 opset 版本限制

**现象**：导出 ONNX 时用了默认 opset=12 或更高，地平线工具链编译报错。

**原因**：地平线 J5 工具链最高支持 ONNX opset=11，高版本算子不兼容。

**解决方案**：
```python
# 导出时指定 opset=11
torch.onnx.export(model, dummy_input, "yolov5su.onnx", opset_version=11)
```

**经验**：部署到边缘芯片时，工具链的 ONNX opset 支持范围是硬约束。导出前先查文档确认支持的最高版本。

---

### 坑10：模型选型

**现象**：一开始考虑用 YOLOv5s 或更大的模型，但部署后延迟过高。

**原因**：J5 是车规级芯片，算力有限，大模型推理慢。

**解决方案**：选择 YOLOv5su（small + 640输入），在精度和速度之间取得平衡。量化后模型仅 12.5MB。

**经验**：端侧部署的模型选型要从目标硬件的算力出发，不是越大越好。先跑通最小模型，再根据性能余量考虑升级。

---

## 4. 推理开发类

### 坑11：输入类型不匹配导致输出全 0

**现象**：推理能跑通，但输出全是 0 或接近 0，检测不到任何目标。

**原因**：默认以为输入是 float32（归一化到 0-1），实际模型要求 uint8（0-255），BPU 内部做归一化。

**定位过程**：
```cpp
// 打印张量属性
hbDNNGetInputTensorProperties(&prop, dnn_handle, 0);
std::cout << "tensorType: " << prop.tensorType << std::endl;
// 输出：tensorType: 4 → HB_DNN_TENSOR_TYPE_U8
```

**解决方案**：
```cpp
// 错误：float 归一化
// input_data[i] = pixel / 255.0f;

// 正确：直接填 uint8
input_data[c * 640 * 640 + h * 640 + w] = pixel;
```

**经验**：部署模型的第一步就是打印输入输出张量属性（类型、布局、stride、量化参数），不要凭假设写代码。`hbDNNTensorProperties` 结构体里有所有需要的信息。

---

### 坑12：`hbDNNInferCtrlParam_Init` 未声明

**现象**：编译报错：
```
error: 'hbDNNInferCtrlParam_Init' was not declared in this scope
```

**原因**：地平线 SDK 用宏初始化控制参数，不是函数。头文件里定义的是 `HB_DNN_INITIALIZE_INFER_CTRL_PARAM` 宏。

**解决方案**：
```cpp
// 错误
hbDNNInferCtrlParam_Init(&infer_param);

// 正确
HB_DNN_INITIALIZE_INFER_CTRL_PARAM(&infer_param);
```

**经验**：SDK 的初始化接口可能是宏也可能是函数，不确定时 `grep -rn "Init\|INITIALIZE" include/` 搜一下头文件。

---

### 坑13：输出布局确认

**现象**：不确定输出数据的排列方式，解析出来的坐标和类别不对。

**原因**：YOLOv5 的输出有多种可能的布局（NCHW、NHWC、扁平化等），需要确认。

**解决方案**：
```cpp
// 打印输出 stride
for (int i = 0; i < prop.validShape.numDimensions; i++)
    std::cout << prop.stride[i] << " ";
// 输出：[2822400, 33600, 8400, 1] → NCHW 布局

// 正确解析方式
float conf = sigmoid(output_data[c * 8400 + i]);  // c=4..83 是类别
float cx = output_data[0 * 8400 + i];
float cy = output_data[1 * 8400 + i];
```

**经验**：stride 数组直接揭示了内存布局。`stride[0]` 最大说明 C 是最外层维度，即 NCHW。

---

### 坑14：量化精度对齐

**现象**：量化后模型检测结果和原模型有差异，担心精度损失。

**解决方案**：地平线工具链编译时会输出量化前后的余弦相似度：
```
cosine similarity: 0.999
```
0.999 表示量化精度损失极小，可以接受。

**经验**：PTQ 量化的精度验证是部署的必要环节。余弦相似度 > 0.99 通常可以接受，低于这个值需要检查校准数据集或考虑 QAT。

---

## 5. CAN 通信类

### 坑15：SocketCAN 协议设计

**现象**：检测框坐标可能超过 255，单字节放不下。

**解决方案**：设计双帧协议，坐标用 uint16 小端编码：
```cpp
// 帧1（0x100）：idx, class, conf, x1(2B), y1(2B), 0x01
// 帧2（0x101）：idx, x2(2B), y2(2B), 0x00, 0x00, 0x02
uint16_t x1 = (uint16_t)box.x1;
f1[3] = x1 & 0xFF;        // 低字节
f1[4] = (x1 >> 8) & 0xFF; // 高字节
```

**经验**：CAN 标准帧只有 8 字节数据，大数值要拆成多字节。小端模式（低字节在前）是 x86/ARM 的默认字节序，接收端直接 `memcpy` 或位运算重组即可。

---

### 坑16：candump 验证

**现象**：CAN 发送了但不确定数据对不对。

**解决方案**：
```bash
# 后台监听
candump vcan0 > can_dump.txt &

# 运行推理后查看
cat can_dump.txt
```
逐字节核对：
```
vcan0  100   [8]  00 02 47 F0 00 23 01 01
# idx=0, class=2(car), conf=0x47=71%, x1=0x00F0=240, y1=0x0123=291
```

**经验**：硬件/总线通信一定要有验证手段。candump 是最直接的验证方式，逐字节解析能确认协议实现是否正确。

---

## 6. 结果调优类

### 坑17：NMS 阈值调优

**现象**：初始阈值 conf=0.25, iou=0.45 时，检测框密集重叠，误检多。

**解决方案**：
```bash
# 调整置信度阈值
sed -i '124s/0\.25/0.55/' infer_demo.cpp
# 调整 NMS IoU 阈值
sed -i '139s/0\.45/0.5/' infer_demo.cpp
```

调整后结果：
- 候选框从大量减少到 20 个
- NMS 后最终 2 个目标（car:0.71, person:0.61）
- 误检显著减少

**经验**：阈值没有标准答案，要根据实际场景调。先看最大置信度分布，再定置信度阈值；IoU 阈值影响重叠框的合并程度，0.5 是常用起点。

---

### 坑18：批量测试验证稳定性

**现象**：单图测试通过，但不确定模型在不同场景下的稳定性。

**解决方案**：编写批量测试程序，遍历 30 张测试图：
```cpp
for (int i = 1; i <= 30; i++) {
    std::string path = "/workspace/dataset/test/test_" + 
                       std::string(i < 10 ? "0" : "") + std::to_string(i) + ".jpg";
    // 推理 + 统计目标数 + 记录延迟
}
```

结果：30 张图 / 424 个目标 / 零空检测 / 平均延迟 6324ms（仿真）。

**经验**：单图通过 ≠ 部署完成。批量测试能暴露模型在不同光照、角度、场景下的鲁棒性问题，是部署验证的必要环节。

---

## 总结：踩坑方法论

1. **先读头文件**：不确定 API 时，`grep -rn` 搜头文件比猜更高效
2. **打印张量属性**：部署第一步就是确认输入输出的类型、布局、量化参数
3. **分层定位**：输出不对时，先确认输入对不对 → 再确认推理跑没跑 → 最后查后处理
4. **保留验证手段**：每个环节都要有可观测的验证方式（打印、candump、批量测试）
5. **记录问题**：踩过的坑写下来，既是知识传承也是面试素材

# 完整部署流程

> 从 PyTorch 模型到地平线 J5 BPU 上运行的完整步骤。

---

## 阶段一：环境准备

### 1.1 拉取地平线工具链镜像

```bash
docker pull openexplorer/ai_toolchain_ubuntu_20_j5_cpu:v1.1.77-py38
```

### 1.2 启动容器（特权模式，支持 CAN）

```bash
docker run -it --name horizon_j5 --privileged \
  -v ~/workspace:/workspace \
  openexplorer/ai_toolchain_ubuntu_20_j5_cpu:v1.1.77-py38 /bin/bash
```

### 1.3 安装依赖（容器内首次）

```bash
apt update
apt install -y iproute2 can-utils libopencv-dev
```

### 1.4 设置虚拟 CAN

```bash
ip link add dev vcan0 type vcan
ip link set up vcan0
ip link show vcan0  # 验证
```

---

## 阶段二：模型导出

### 2.1 PyTorch → ONNX

```python
import torch
import sys
sys.path.insert(0, 'yolov5')
from models.experimental import attempt_load

model = attempt_load('yolov5su.pt', map_location='cpu')
model.eval()

dummy_input = torch.randn(1, 3, 640, 640)
torch.onnx.export(
    model, dummy_input, 'yolov5su.onnx',
    opset_version=11,          # 地平线工具链最高支持 11
    input_names=['images'],
    output_names=['output'],
    do_constant_folding=True
)
```

### 2.2 验证 ONNX

```bash
python -c "
import onnx
m = onnx.load('yolov5su.onnx')
onnx.checker.check_model(m)
print('ONNX 验证通过，opset:', m.opset_import[0].version)
"
```

---

## 阶段三：量化编译

### 3.1 准备校准数据集

准备 100~500 张有代表性的图片，放在 `calibration_data/` 目录。

### 3.2 编写编译配置（yaml）

```yaml
model:
  model_name: yolov5su
  model_type: onnx
  onnx_model: yolov5su.onnx

parameters:
  version: 1.0
  optimize: true
  quantize: true
  calibration_data: calibration_data/
  input_type: uint8         # 输入为 uint8，BPU 内部归一化
  input_layout: NCHW

output:
  output_dir: model_output/
```

### 3.3 执行编译

```bash
hb_mapper makertbin --config yolov5su.yaml --model-type onnx
```

### 3.4 检查输出

```bash
ls -la model_output/
# yolov5su_j5.bin  (约 12.5MB，INT8 量化后)
```

编译日志中会输出量化精度：
```
cosine similarity: 0.999
```

---

## 阶段四：C++ 推理开发

### 4.1 确认张量属性

```cpp
hbDNNTensorProperties input_prop, output_prop;
hbDNNGetInputTensorProperties(&input_prop, dnn_handle, 0);
hbDNNGetOutputTensorProperties(&output_prop, dnn_handle, 0);

// 关键信息
std::cout << "输入类型: " << input_prop.tensorType << std::endl;  // 4 = uint8
std::cout << "输出布局 stride: ";
for (int i = 0; i < 4; i++) std::cout << output_prop.stride[i] << " ";
// [2822400, 33600, 8400, 1] → NCHW
```

### 4.2 内存分配

```cpp
hbSysMem input_mem, output_mem;
hbSysAllocCachedMem(&input_mem, input_prop.alignedByteSize);   // 1,228,800 字节
hbSysAllocCachedMem(&output_mem, output_prop.alignedByteSize); // 2,822,400 字节
```

### 4.3 输入填充（uint8, NCHW, BGR→RGB）

```cpp
cv::Mat img = cv::imread(image_path);
cv::resize(img, img, cv::Size(640, 640));
uint8_t* input_data = (uint8_t*)input_mem.virAddr;

for (int c = 0; c < 3; c++)
    for (int h = 0; h < 640; h++)
        for (int w = 0; w < 640; w++) {
            cv::Vec3b p = img.at<cv::Vec3b>(h, w);
            input_data[c * 640 * 640 + h * 640 + w] = p[2 - c]; // BGR→RGB
        }
hbSysFlushMem(&input_mem, HB_SYS_MEM_CACHE_CLEAN);
```

### 4.4 执行推理

```cpp
hbDNNInferCtrlParam infer_param;
HB_DNN_INITIALIZE_INFER_CTRL_PARAM(&infer_param);  // 注意是宏，不是函数

hbDNNTaskHandle_t task_handle = nullptr;
hbDNNTensor* output_ptr = &output_tensor;
hbDNNInfer(&task_handle, &output_ptr, &input_tensor, dnn_handle, &infer_param);
hbDNNWaitTaskDone(task_handle, 0);
hbDNNReleaseTask(task_handle);
```

### 4.5 输出解析（NCHW: output[c*8400+i]）

```cpp
hbSysFlushMem(&output_mem, HB_SYS_MEM_CACHE_INVALIDATE);
float* output_data = (float*)output_mem.virAddr;

for (int i = 0; i < 8400; i++) {
    float max_conf = 0; int max_class = 0;
    for (int c = 4; c < 84; c++) {
        float conf = sigmoid(output_data[c * 8400 + i]);
        if (conf > max_conf) { max_conf = conf; max_class = c - 4; }
    }
    if (max_conf < 0.55) continue;  // 置信度阈值

    float cx = output_data[0 * 8400 + i];
    float cy = output_data[1 * 8400 + i];
    float w  = output_data[2 * 8400 + i];
    float h  = output_data[3 * 8400 + i];
    // 转成 x1,y1,x2,y2，加入候选列表
}

// NMS 去重
std::vector<DetectBox> final_boxes = nms(boxes, 0.5);
```

---

## 阶段五：CAN 输出

### 5.1 初始化 SocketCAN

```cpp
int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
struct ifreq ifr;
strcpy(ifr.ifr_name, "vcan0");
ioctl(sock, SIOCGIFINDEX, &ifr);

struct sockaddr_can addr;
addr.can_family = AF_CAN;
addr.can_ifindex = ifr.ifr_ifindex;
bind(sock, (struct sockaddr*)&addr, sizeof(addr));
```

### 5.2 发送检测结果

```cpp
// 每个目标发两帧（坐标拆分）
// 帧1 0x100: idx, class, conf, x1(2B), y1(2B), 0x01
// 帧2 0x101: idx, x2(2B), y2(2B), 0x00, 0x00, 0x02
// 结束帧 0x102: total, 0xFF×7
```

### 5.3 验证

```bash
candump vcan0
# 应看到 0x100、0x101、0x102 三种帧
```

---

## 阶段六：批量测试验证

```bash
cd /workspace/infer/build
./batch_test
```

输出示例：
```
[1/30] test_01.jpg - 2个目标 6497ms (car:0.71, person:0.61)
...
[30/30] test_30.jpg - 1个目标 6290ms (car:0.67)
========== 汇总 ==========
测试图片: 30张 | 检测目标: 424个 | 平均: 14.1个/张
平均延迟: 6324ms/张（仿真模式）
无检测图片: 0张
```

---

## 流程总览

```
PyTorch(.pt)
    │ ① 导出 ONNX (opset=11)
    ▼
ONNX(.onnx)
    │ ② 地平线工具链量化编译 (PTQ INT8 + 精度对齐)
    ▼
BPU模型(.bin)  ← 余弦相似度 0.999
    │ ③ C++ 推理程序加载
    ▼
内存分配 → 输入填充(uint8 NCHW) → BPU推理 → 输出解析(sigmoid+NMS)
    │
    ▼
检测结果 (类别+置信度+坐标)
    │ ④ SocketCAN 双帧协议
    ▼
车载 CAN 总线 (0x100/0x101/0x102)
    │
    ▼
candump 验证 ✓
```

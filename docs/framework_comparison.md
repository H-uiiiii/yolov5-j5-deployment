# 多推理引擎横向对比报告

> C++ 实现 ONNX Runtime / NCNN / MNN / LibTorch 四框架 YOLOv5n 推理流水线，
> 完成 CPU / Vulkan / OpenCL 多后端性能对比。

---

## 测试环境

| 项目 | 配置 |
|------|------|
| CPU | AMD 多核处理器 |
| GPU | AMD GPU（Vulkan/OpenCL） |
| 模型 | YOLOv5n（INT8 量化） |
| 测试集 | 30 张图片，统一 640×640 输入 |
| 测试方法 | 预热 5 次后取 30 次平均值 |

---

## 测试配置（5 种）

| 配置 | 推理引擎 | 后端 | 线程数 |
|------|---------|------|--------|
| C1 | ONNX Runtime | CPU | 4 |
| C2 | NCNN | CPU | 4 |
| C3 | MNN | CPU | 4 |
| C4 | LibTorch | CPU | 4 |
| C5 | NCNN | Vulkan (GPU) | - |

---

## 核心指标对比（7 项）

| 指标 | ONNX Runtime | NCNN (CPU) | MNN (CPU) | LibTorch | NCNN (Vulkan) |
|------|-------------|------------|-----------|----------|---------------|
| 推理延迟 | ~45ms | **28.7ms** | 32ms | ~60ms | 12.9ms |
| 吞吐量 (FPS) | ~22 | 34.8 | 31.3 | ~17 | **77.6** |
| 内存占用 | **109MB** | 135MB | 128MB | 180MB | 210MB |
| CPU 利用率 | 65% | 78% | 72% | 85% | 20% |
| 初始化时间 | 1.2s | 0.8s | 0.6s | 2.5s | 1.5s |
| 模型加载 | 12MB | 12MB | 12MB | 28MB | 12MB |
| 跨平台 | ★★★★★ | ★★★★★ | ★★★★★ | ★★★☆☆ | ★★★★☆ |

---

## 关键结论

### 1. CPU 推理：MNN 最快，ONNX Runtime 最省内存

- **MNN** 延迟最低（28.7ms），吞吐量 34.8 FPS
- **ONNX Runtime** 内存占用最低（109MB），适合资源受限的嵌入式设备
- **LibTorch** 延迟最高（~60ms），且模型文件最大（28MB），不适合端侧部署

### 2. GPU 加速：NCNN Vulkan 提升 6.4 倍

- NCNN Vulkan 后端在 AMD GPU 上达到 **77.6 FPS**
- 较 CPU 版本（12.1 FPS）提升 **6.4 倍**
- GPU 推理时 CPU 利用率仅 20%，可释放 CPU 资源给其他任务

### 3. 多线程加速比 2.2~2.6 倍

| 引擎 | 单线程 | 4线程 | 加速比 |
|------|--------|-------|--------|
| ONNX Runtime | 98ms | 45ms | 2.18x |
| NCNN | 72ms | 28.7ms | 2.51x |
| MNN | 80ms | 32ms | 2.50x |
| LibTorch | 140ms | 60ms | 2.33x |

### 4. Amdahl 定律定位瓶颈

多线程加速比未达到理论 4x，用 Amdahl 定律分析：

```
加速比 = 1 / (串行比例 + 并行比例 / 线程数)
2.5 = 1 / (S + (1-S) / 4)
→ S ≈ 0.20 （串行部分约占 20%）
```

实测 NMS 后处理为纯串行计算，约占总时间的 **25%**，是主要瓶颈。优化方向：
- NMS 并行化（按类别分组处理）
- 用 GPU 做 NMS
- 减少候选框数量（提高置信度阈值）

---

## 踩坑：DirectML 版本兼容

最初尝试 ONNX Runtime DirectML 后端，遇到版本兼容问题：
- DirectML 1.x 与 ONNX Runtime 1.1x 不兼容
- 算子支持不全，部分层回退到 CPU

**解决方案**：切换到 NCNN Vulkan 后端，稳定性更好，AMD GPU 兼容性更佳。

---

## 引擎选型建议

| 场景 | 推荐引擎 | 理由 |
|------|---------|------|
| 嵌入式 Linux（CPU only） | MNN | 延迟最低，轻量级 |
| 内存极度受限 | ONNX Runtime | 内存占用最低 |
| Android/iOS | NCNN | 移动端优化最好 |
| 有 GPU 加速 | NCNN Vulkan | 跨平台 GPU 支持 |
| 快速验证/研究 | LibTorch | 与 PyTorch 无缝衔接 |
| 地平线 J5 BPU | 地平线 SDK | 专用芯片，性能最优 |

---

## 与地平线 J5 BPU 的对比

| 平台 | 延迟 | 说明 |
|------|------|------|
| 地平线 J5 BPU（仿真） | ~6300ms | x86 仿真模式，开发板上会快很多 |
| AMD CPU（MNN） | 28.7ms | 4 线程 |
| AMD GPU（NCNN Vulkan） | 12.9ms | Vulkan 后端 |

> 注：J5 仿真模式的延迟不代表真实芯片性能。真实 J5 开发板上 YOLOv5s 可达到数十毫秒级。

---

## 测试脚本使用

```bash
# 编译
mkdir build && cd build
cmake .. && make -j4

# 运行所有配置
./framework_benchmark --model ../model/yolov5n.bin --images ../test_images/

# 指定引擎和后端
./framework_benchmark --engine ncnn --backend vulkan --threads 4
```

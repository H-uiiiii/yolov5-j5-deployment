# 模型导出与量化编译说明

## 模型来源

- 基础模型：YOLOv5su（Ultralytics 官方预训练权重）
- 输入尺寸：640×640
- 输出：84×8400（4 坐标 + 80 类别，8400 锚点）

## 导出 ONNX

```bash
cd model
python export_onnx.py --weights yolov5su.pt --opset 11 --img 640
```

> 注意：地平线 J5 工具链最高支持 ONNX opset=11，不要用更高版本。

## 量化编译（地平线工具链）

```bash
# 容器内执行
hb_mapper makertbin --config yolov5su.yaml --model-type onnx
```

编译配置（yolov5su.yaml）：
```yaml
model:
  model_name: yolov5su
  model_type: onnx
  onnx_model: yolov5su.onnx
parameters:
  quantize: true
  calibration_data: calibration_data/
  input_type: uint8
output:
  output_dir: ../model_output/
```

## 输出文件

| 文件 | 大小 | 说明 |
|------|------|------|
| yolov5su_j5.bin | ~12.5MB | INT8 量化后的 BPU 可执行模型 |

## 量化精度

编译日志输出余弦相似度：
```
cosine similarity: 0.999
```

> 余弦相似度 > 0.99 表示量化精度损失可接受。

## 校准数据集

- 数量：100~500 张有代表性的图片
- 来源：与测试场景相似的图片（街道、车辆、行人等）
- 格式：JPG/PNG，工具链会自动 resize

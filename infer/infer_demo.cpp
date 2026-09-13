#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "hb_dnn.h"
#include "hb_sys.h"
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

struct DetectBox {
    float x1, y1, x2, y2;
    float conf;
    int class_id;
};

const char* class_names[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake",
    "chair", "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop",
    "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

float calc_iou(const DetectBox& a, const DetectBox& b) {
    float x1 = std::max(a.x1, b.x1), y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2), y2 = std::min(a.y2, b.y2);
    float w = std::max(0.0f, x2 - x1), h = std::max(0.0f, y2 - y1);
    float inter = w * h;
    float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
    return inter / (area_a + area_b - inter + 1e-6f);
}

std::vector<DetectBox> nms(std::vector<DetectBox>& boxes, float iou_thresh) {
    std::sort(boxes.begin(), boxes.end(), [](const DetectBox& a, const DetectBox& b){ return a.conf > b.conf; });
    std::vector<DetectBox> result;
    std::vector<bool> suppressed(boxes.size(), false);
    for (size_t i = 0; i < boxes.size(); i++) {
        if (suppressed[i]) continue;
        result.push_back(boxes[i]);
        for (size_t j = i + 1; j < boxes.size(); j++) {
            if (suppressed[j]) continue;
            if (boxes[i].class_id == boxes[j].class_id && calc_iou(boxes[i], boxes[j]) > iou_thresh)
                suppressed[j] = true;
        }
    }
    return result;
}

// ========== CAN 功能 ==========
static int can_socket = -1;

int can_init(const char* ifname) {
    struct sockaddr_can addr;
    struct ifreq ifr;
    can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_socket < 0) { std::cerr << "[CAN] socket创建失败" << std::endl; return -1; }
    strcpy(ifr.ifr_name, ifname);
    ioctl(can_socket, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[CAN] 绑定失败" << std::endl;
        close(can_socket); can_socket = -1; return -1;
    }
    std::cout << "[CAN] 初始化成功，接口: " << ifname << std::endl;
    return 0;
}

int can_send_frame(uint32_t can_id, const uint8_t* data, uint8_t len) {
    if (can_socket < 0) return -1;
    struct can_frame frame;
    frame.can_id = can_id;
    frame.can_dlc = len;
    memcpy(frame.data, data, len);
    int nbytes = write(can_socket, &frame, sizeof(frame));
    return (nbytes == sizeof(frame)) ? 0 : -1;
}

void can_send_detections(const std::vector<DetectBox>& boxes) {
    if (can_socket < 0) { std::cout << "[CAN] 未初始化，跳过" << std::endl; return; }
    uint8_t total = (uint8_t)boxes.size();
    std::cout << "[CAN] 开始发送 " << (int)total << " 个目标" << std::endl;
    for (uint8_t idx = 0; idx < total; idx++) {
        const DetectBox& box = boxes[idx];
        // 帧1: 类别+置信度+x1+y1
        uint8_t f1[8] = {0};
        f1[0] = idx;
        f1[1] = (uint8_t)box.class_id;
        f1[2] = (uint8_t)(box.conf * 100);
        uint16_t x1 = (uint16_t)std::max(0.0f, std::min(65535.0f, box.x1));
        uint16_t y1 = (uint16_t)std::max(0.0f, std::min(65535.0f, box.y1));
        f1[3] = x1 & 0xFF; f1[4] = (x1 >> 8) & 0xFF;
        f1[5] = y1 & 0xFF; f1[6] = (y1 >> 8) & 0xFF;
        f1[7] = 0x01;
        can_send_frame(0x100, f1, 8);
        // 帧2: x2+y2
        uint8_t f2[8] = {0};
        f2[0] = idx;
        uint16_t x2 = (uint16_t)std::max(0.0f, std::min(65535.0f, box.x2));
        uint16_t y2 = (uint16_t)std::max(0.0f, std::min(65535.0f, box.y2));
        f2[1] = x2 & 0xFF; f2[2] = (x2 >> 8) & 0xFF;
        f2[3] = y2 & 0xFF; f2[4] = (y2 >> 8) & 0xFF;
        f2[7] = 0x02;
        can_send_frame(0x101, f2, 8);
        std::cout << "[CAN] 目标" << (int)idx << ": " << class_names[box.class_id]
                  << " conf=" << (int)(box.conf*100) << "% 已发送" << std::endl;
    }
    // 结束帧
    uint8_t end[8] = {total, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    can_send_frame(0x102, end, 8);
    std::cout << "[CAN] 发送完成，结束标志ID=0x102" << std::endl;
}

void can_close() { if (can_socket >= 0) { close(can_socket); can_socket = -1; } }

// ========== 主函数 ==========
int main() {
    const char* model_path = "/workspace/model_output/yolov5su_j5.bin";
    const char* image_path = "/workspace/dataset/test/test_01.jpg";

    // [1/7] 加载模型
    hbPackedDNNHandle_t packed_handle = nullptr;
    hbDNNInitializeFromFiles(&packed_handle, &model_path, 1);
    char const** name_list = nullptr;
    int32_t count = 0;
    hbDNNGetModelNameList(&name_list, &count, packed_handle);
    hbDNNHandle_t dnn_handle = nullptr;
    hbDNNGetModelHandle(&dnn_handle, packed_handle, name_list[0]);
    std::cout << "[1/7] 模型加载成功" << std::endl;

    // [2/7] 内存分配
    hbDNNTensor input_tensor, output_tensor;
    hbDNNTensorProperties input_prop, output_prop;
    hbDNNGetInputTensorProperties(&input_prop, dnn_handle, 0);
    hbDNNGetOutputTensorProperties(&output_prop, dnn_handle, 0);
    hbSysMem input_mem, output_mem;
    hbSysAllocCachedMem(&input_mem, input_prop.alignedByteSize);
    hbSysAllocCachedMem(&output_mem, output_prop.alignedByteSize);
    input_tensor.properties = input_prop;
    input_tensor.sysMem[0] = input_mem;
    output_tensor.properties = output_prop;
    output_tensor.sysMem[0] = output_mem;
    std::cout << "[2/7] 内存分配完成（输入uint8 " << input_prop.alignedByteSize
              << "字节，输出float32 " << output_prop.alignedByteSize << "字节）" << std::endl;

    // [3/7] 输入填充（uint8, NCHW, BGR→RGB）
    cv::Mat img = cv::imread(image_path);
    cv::Mat img_resized;
    cv::resize(img, img_resized, cv::Size(640, 640));
    uint8_t* input_data = (uint8_t*)input_mem.virAddr;
    for (int c = 0; c < 3; c++)
        for (int h = 0; h < 640; h++)
            for (int w = 0; w < 640; w++) {
                cv::Vec3b p = img_resized.at<cv::Vec3b>(h, w);
                input_data[c * 640 * 640 + h * 640 + w] = p[2 - c];
            }
    hbSysFlushMem(&input_mem, HB_SYS_MEM_CACHE_CLEAN);
    std::cout << "[3/7] 输入数据填充完成（uint8 0-255，BPU内部归一化）" << std::endl;

    // [4/7] 推理
    hbDNNInferCtrlParam infer_param;
    HB_DNN_INITIALIZE_INFER_CTRL_PARAM(&infer_param);
    hbDNNTaskHandle_t task_handle = nullptr;
    hbDNNTensor* output_ptr = &output_tensor;
    auto t0 = std::chrono::high_resolution_clock::now();
    hbDNNInfer(&task_handle, &output_ptr, &input_tensor, dnn_handle, &infer_param);
    hbDNNWaitTaskDone(task_handle, 0);
    hbDNNReleaseTask(task_handle);
    auto t1 = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    std::cout << "[4/7] 推理完成，延迟: " << ms << " ms (仿真模式)" << std::endl;

    // [5/7] 输出解析（NCHW: output[c*8400+i]）
    hbSysFlushMem(&output_mem, HB_SYS_MEM_CACHE_INVALIDATE);
    float* output_data = (float*)output_mem.virAddr;
    std::vector<DetectBox> boxes;
    float max_conf_all = 0;
    for (int i = 0; i < 8400; i++) {
        float max_conf = 0; int max_class = 0;
        for (int c = 4; c < 84; c++) {
            float conf = sigmoid(output_data[c * 8400 + i]);
            if (conf > max_conf) { max_conf = conf; max_class = c - 4; }
        }
        if (max_conf > max_conf_all) max_conf_all = max_conf;
        if (max_conf < 0.55) continue;
        float cx = output_data[0 * 8400 + i];
        float cy = output_data[1 * 8400 + i];
        float w = output_data[2 * 8400 + i];
        float h = output_data[3 * 8400 + i];
        DetectBox box;
        box.x1 = cx - w / 2; box.y1 = cy - h / 2;
        box.x2 = cx + w / 2; box.y2 = cy + h / 2;
        box.conf = max_conf; box.class_id = max_class;
        boxes.push_back(box);
    }
    std::cout << "所有候选框最大置信度: " << max_conf_all << std::endl;
    std::cout << "[5/7] 过滤后候选框(>0.55): " << boxes.size() << " 个" << std::endl;

    // [6/7] NMS + CAN发送
    std::vector<DetectBox> final_boxes = nms(boxes, 0.5);
    std::cout << "[6/7] NMS后最终检测框: " << final_boxes.size() << " 个" << std::endl;
    for (const auto& box : final_boxes)
        std::cout << "  检测到: " << class_names[box.class_id] << ": "
                  << std::to_string(box.conf).substr(0, 4)
                  << "  位置:(" << (int)box.x1 << "," << (int)box.y1 << ","
                  << (int)box.x2 << "," << (int)box.y2 << ")" << std::endl;

    // 初始化CAN并发送结果
    can_init("vcan0");
    can_send_detections(final_boxes);
    can_close();

    // [7/7] 可视化
    for (const auto& box : final_boxes) {
        cv::rectangle(img_resized, cv::Point((int)box.x1, (int)box.y1),
                      cv::Point((int)box.x2, (int)box.y2), cv::Scalar(0, 255, 0), 2);
        std::string label = class_names[box.class_id] + std::string(": ") + std::to_string(box.conf).substr(0, 4);
        cv::putText(img_resized, label, cv::Point((int)box.x1, (int)box.y1 - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }
    cv::imwrite("/workspace/infer/result.jpg", img_resized);
    std::cout << "[7/7] 结果图已保存: /workspace/infer/result.jpg" << std::endl;

    hbSysFreeMem(&input_mem);
    hbSysFreeMem(&output_mem);
    hbDNNRelease(packed_handle);
    return 0;
}

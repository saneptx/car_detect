#ifndef YOLO_TRT_H
#define YOLO_TRT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// 定义一个结构体来表示检测到的目标框
// 确保这个结构体足够通用，以便于服务器解析
typedef struct {
    int xmin;
    int ymin;
    int xmax;
    int ymax;
    float confidence;
    int class_id;
} yolo_detection_t;

// 推理上下文句柄
typedef void* trt_context_t;

// 1. 初始化 TensorRT Engine 和 CUDA 资源
// engine_path: .engine 文件路径
// max_detections: 预期的最大检测框数量
trt_context_t yolo_init(const char* engine_path, int width, int height, int max_detections);

// 2. 执行推理并获取结果
// context: 初始化后的上下文
// mjpeg_data/size: MJPEG 压缩数据
// width/height: 原始图像尺寸
// detections: 输出数组，用于存储检测结果
// max_detections: detections 数组的最大容量
// 返回值: 实际检测到的目标数量
int yolo_infer_frame(
    trt_context_t context,
    const unsigned char* mjpeg_data,
    size_t mjpeg_size,
    int width,
    int height,
    yolo_detection_t* detections,
    int max_detections
);

// 3. 清理资源
void yolo_cleanup(trt_context_t context);

#endif // YOLO_TRT_H
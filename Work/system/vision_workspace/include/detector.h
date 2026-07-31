/*
 * detector.h - YOLOv5检测和ByteTrack追踪模块
 *
 * 功能：
 * - YOLOv5 NCNN模型推理
 * - ByteTrack多目标追踪
 * - NMS非极大值抑制
 */

#ifndef DETECTOR_H
#define DETECTOR_H

#include "common.h"
#include <opencv2/opencv.hpp>
#include <net.h>
#include <vector>

//============================================================
//            YOLOv5检测结果结构
//============================================================

struct obj_confect {
    cv::Rect_<float> rect;
    int label;
    float prob;
};

//============================================================
//            ByteTrack追踪结构
//============================================================

struct Trackobj_confect {
    float cx, cy, w, h;
    int track_id;
    int label;
    float score;
    int lost_frames;
    float kf[8];  // 卡尔曼滤波器状态 [x, y, w, h, vx, vy, vw, vh]
};

//============================================================
//            ByteTrack多目标追踪器
//============================================================

class ByteTrack {
public:
    std::vector<Trackobj_confect> tracks;
    int next_id = 1;
    const int max_lost = 60;      // 丢失容忍度
    const float iou_thresh = 0.20f;  // IOU 阈值

    void predict_only();
    std::vector<Trackobj_confect> update(const std::vector<obj_confect>& detections);

private:
    float compute_iou(const obj_confect& det, const Trackobj_confect& trk);
};

//============================================================
//            YOLOv5检测器
//============================================================

class YoloDetector {
public:
    /**
     * 加载YOLOv5模型
     * @param param_path .param文件路径
     * @param model_path .bin文件路径
     * @param num_threads NCNN线程数
     * @return 0成功，-1失败
     */
    int load(const char* param_path, const char* model_path, int num_threads = 2);

    /**
     * 执行目标检测
     * @param bgr 输入图像(BGR格式)
     * @param objects 输出检测结果
     * @return 0成功
     */
    int detect(const cv::Mat& bgr, std::vector<obj_confect>& objects);

    bool isLoaded() const { return loaded_; }

private:
    ncnn::Net net_;
    bool loaded_ = false;

    static inline float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

    void generate_proposals(const ncnn::Mat& anchors, int stride,
                            const ncnn::Mat& feat_blob, float prob_threshold,
                            std::vector<obj_confect>& objects);
    void nms_sorted_bboxes(const std::vector<obj_confect>& objects,
                           std::vector<int>& picked, float nms_threshold);
};

//============================================================
//            类别名称
//============================================================

extern const char* class_names[];

#endif // DETECTOR_H

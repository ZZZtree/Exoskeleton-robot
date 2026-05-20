/*
 * detector.cpp - YOLOv5检测和ByteTrack追踪模块实现
 */

#include "detector.h"

//============================================================
//            类别名称定义
//============================================================

const char* class_names[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

//============================================================
//            ByteTrack实现
//============================================================

void ByteTrack::predict_only() {
    for (auto& trk : tracks) {
        trk.lost_frames++;//连续多久没看到了，+1帧
        trk.kf[0] += trk.kf[4];
        trk.kf[1] += trk.kf[5];
        trk.kf[2] += trk.kf[6];
        trk.kf[3] += trk.kf[7];
        trk.cx = trk.kf[0];
        trk.cy = trk.kf[1];
        trk.w  = trk.kf[2];
        trk.h  = trk.kf[3];//进行目标位置预测
    }
}

float ByteTrack::compute_iou(const obj_confect& det, const Trackobj_confect& trk) {//iou计算
    float dx1 = det.rect.x, dy1 = det.rect.y;
    float dx2 = det.rect.x + det.rect.width, dy2 = det.rect.y + det.rect.height;
    float tx1 = trk.cx - trk.w * 0.5f, ty1 = trk.cy - trk.h * 0.5f;
    float tx2 = trk.cx + trk.w * 0.5f, ty2 = trk.cy + trk.h * 0.5f;
    float ix1 = std::max(dx1, tx1), iy1 = std::max(dy1, ty1);
    float ix2 = std::min(dx2, tx2), iy2 = std::min(dy2, ty2);
    float iw = ix2 - ix1, ih = iy2 - iy1;
    if (iw < 0 || ih < 0) return 0;
    return iw * ih / (det.rect.area() + trk.w * trk.h - iw * ih);
}

std::vector<Trackobj_confect> ByteTrack::update(const std::vector<obj_confect>& detections) {
    std::vector<Trackobj_confect> new_tracks;//新的跟踪列表
    std::vector<bool> matched_det(detections.size(), false);//创建一个 “标记列表”，这一帧检测出来多少个目标，并填false，表示一开始检测框全没匹配上，防止一个检测框匹配给多个跟踪目标
    std::vector<bool> matched_trk(tracks.size(), false);//防止一个跟踪目标匹配给多个检测框
//这一帧 YOLO 给出 检测框
//跟踪器拿出上一帧的 跟踪框，先预测位置
//用 IOU 对比：这个检测框 ↔ 这个跟踪框 是不是同一个人
//是的话 → 用检测框修正跟踪框
//最后只把跟踪框画到图上
    // 预测步骤
    for (auto& trk : tracks) {
        trk.lost_frames++;
        trk.kf[0] += trk.kf[4];
        trk.kf[1] += trk.kf[5];
        trk.kf[2] += trk.kf[6];
        trk.kf[3] += trk.kf[7];
        trk.cx = trk.kf[0];
        trk.cy = trk.kf[1];
        trk.w  = trk.kf[2];
        trk.h  = trk.kf[3];
    }

    // 匹配步骤
    for (int d = 0; d < (int)detections.size(); d++) {//遍历所有检测框
        const auto& det = detections[d];//当前这个检测框
        float best_iou = 0;//记录最大重叠
        int best_t = -1;//记录最合适的跟踪框编号

        for (int t = 0; t < (int)tracks.size(); t++) {//遍历所有跟踪框，找和当前检测框最像的
            if (matched_trk[t]) continue;//跟踪框已经匹配过了，跳过
            float iou_val = compute_iou(det, tracks[t]);//计算iou重叠度
            if (iou_val > best_iou && iou_val >= iou_thresh) {//重叠更大，且超过阈值
                best_iou = iou_val;
                best_t = t;//记录
            }
        }

        if (best_t >= 0) {//找到了匹配的
            auto& trk = tracks[best_t];
            trk.cx = det.rect.x + det.rect.width * 0.5f;
            trk.cy = det.rect.y + det.rect.height * 0.5f;
            trk.w  = det.rect.width;
            trk.h  = det.rect.height;
            trk.score = det.prob;
            trk.label = det.label;
            trk.lost_frames = 0;

            // 更高的平滑系数，让跟踪更稳定
            float alpha = 0.90f;
            trk.kf[4] = alpha * (trk.cx - trk.kf[0]) + (1-alpha)*trk.kf[4];
            trk.kf[5] = alpha * (trk.cy - trk.kf[1]) + (1-alpha)*trk.kf[5];
            trk.kf[6] = alpha * (trk.w  - trk.kf[2]) + (1-alpha)*trk.kf[6];
            trk.kf[7] = alpha * (trk.h  - trk.kf[3]) + (1-alpha)*trk.kf[7];
            trk.kf[0] = alpha * trk.cx + (1-alpha)*trk.kf[0];
            trk.kf[1] = alpha * trk.cy + (1-alpha)*trk.kf[1];
            trk.kf[2] = alpha * trk.w  + (1-alpha)*trk.kf[2];
            trk.kf[3] = alpha * trk.h  + (1-alpha)*trk.kf[3];

            matched_det[d] = true;//检测框和跟踪框已用了
            matched_trk[best_t] = true;
        }
    }

    // 保留未匹配但未超时的轨迹
    for (int t = 0; t < (int)tracks.size(); t++) {
        if (matched_trk[t] || tracks[t].lost_frames < max_lost)
            new_tracks.push_back(tracks[t]);//这一阵匹配上了，或者丢帧的时间不长，就把目标保留到下一帧
    }

    // 新增检测创建新轨迹
    for (int d = 0; d < (int)detections.size(); d++) {//遍历所有检测框
        if (!matched_det[d]) {//如果没匹配上，说明是新出现的
            const auto& det = detections[d];
            Trackobj_confect tobj;//创建一个新的跟踪对象，把检测框位置赋值给跟踪框。后续将由跟踪框进行跟进
            tobj.cx = det.rect.x + det.rect.width * 0.5f;
            tobj.cy = det.rect.y + det.rect.height * 0.5f;
            tobj.w  = det.rect.width;
            tobj.h  = det.rect.height;
            tobj.score = det.prob;
            tobj.label = det.label;
            tobj.track_id = next_id++;
            tobj.lost_frames = 0;
            tobj.kf[0] = tobj.cx;
            tobj.kf[1] = tobj.cy;
            tobj.kf[2] = tobj.w;
            tobj.kf[3] = tobj.h;
            tobj.kf[4] = 0; tobj.kf[5] = 0; tobj.kf[6] = 0; tobj.kf[7] = 0;
            new_tracks.push_back(tobj);
        }
    }

    tracks.swap(new_tracks);//将匹配后的id图输出
    return tracks;
}

//============================================================
//            YOLOv5检测器实现
//============================================================

int YoloDetector::load(const char* param_path, const char* model_path, int num_threads) {
    net_.opt.num_threads = num_threads;
    net_.opt.use_fp16_packed = false;
    net_.opt.use_fp16_storage = false;
    net_.opt.use_bf16_storage = false;

    if (net_.load_param(param_path) || net_.load_model(model_path)) {
        fprintf(stderr, "[YOLOv5] 模型加载失败: %s, %s\n", param_path, model_path);
        return -1;
    }

    loaded_ = true;
    printf("[YOLOv5] 模型加载成功: %s, 线程数: %d\n", param_path, num_threads);
    return 0;
}

void YoloDetector::nms_sorted_bboxes(const std::vector<obj_confect>& objects,
                                      std::vector<int>& picked, float nms_threshold) {
    picked.clear();
    int n = objects.size();
    std::vector<float> areas(n);
    for (int i = 0; i < n; i++) areas[i] = objects[i].rect.area();

    for (int i = 0; i < n; i++) {
        int keep = 1;
        for (int j : picked) {
            cv::Rect_<float> inter = objects[i].rect & objects[j].rect;
            float inter_area = inter.area();
            float union_area = areas[i] + areas[j] - inter_area;
            if (union_area > 0 && inter_area / union_area > nms_threshold) {
                keep = 0; break;
            }
        }
        if (keep) picked.push_back(i);
    }
}

void YoloDetector::generate_proposals(const ncnn::Mat& anchors, int stride,
                                       const ncnn::Mat& feat_blob, float prob_threshold,
                                       std::vector<obj_confect>& objects) {
    int num_grid_x = feat_blob.w;
    int num_grid_y = feat_blob.h;
    int num_anchors = 3;
    int num_class = 80;

    for (int a = 0; a < num_anchors; a++) {
        float aw = anchors[a*2+0];
        float ah = anchors[a*2+1];
        int c = a * 85;
        for (int y = 0; y < num_grid_y; y++) {
            for (int x = 0; x < num_grid_x; x++) {
                float obj_conf = sigmoid(feat_blob.channel(c+4).row(y)[x]);
                if (obj_conf < prob_threshold) continue;

                float cls_score = 0;
                int cls_id = 0;
                for (int k = 0; k < num_class; k++) {
                    float s = sigmoid(feat_blob.channel(c+5+k).row(y)[x]);
                    if (s > cls_score) { cls_score = s; cls_id = k; }
                }
                float conf = obj_conf * cls_score;
                if (conf < prob_threshold) continue;

                float dx = sigmoid(feat_blob.channel(c+0).row(y)[x]);
                float dy = sigmoid(feat_blob.channel(c+1).row(y)[x]);
                float dw = sigmoid(feat_blob.channel(c+2).row(y)[x]);
                float dh = sigmoid(feat_blob.channel(c+3).row(y)[x]);

                float cx = (dx*2 - 0.5f + x) * stride;
                float cy = (dy*2 - 0.5f + y) * stride;
                float w  = powf(dw*2, 2) * aw;
                float h  = powf(dh*2, 2) * ah;

                obj_confect obj;
                obj.rect.x = cx - w*0.5f;
                obj.rect.y = cy - h*0.5f;
                obj.rect.width  = w;
                obj.rect.height = h;
                obj.label = cls_id;
                obj.prob  = conf;
                objects.push_back(obj);
            }
        }
    }
}

int YoloDetector::detect(const cv::Mat& bgr, std::vector<obj_confect>& objects) {
    if (!loaded_) return -1;

    const int target_size = 640;
    const float prob_thresh = YOLO_CONF_THRESH;
    const float nms_thresh = YOLO_NMS_THRESH;
    int w = bgr.cols, h = bgr.rows;

    float scale = 1.f;
    if (w > h) { scale = (float)target_size / w; w = target_size; h = (int)(bgr.rows * scale); }
    else       { scale = (float)target_size / h; h = target_size; w = (int)(bgr.cols * scale); }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR2RGB,
                                                  bgr.cols, bgr.rows, w, h);
    int wpad = target_size - w;
    int hpad = target_size - h;
    ncnn::Mat in_pad;
    ncnn::copy_make_border(in, in_pad, hpad/2, hpad-hpad/2,
                           wpad/2, wpad-wpad/2, ncnn::BORDER_CONSTANT, 114.f);

    const float norm[] = {1/255.f, 1/255.f, 1/255.f};
    in_pad.substract_mean_normalize(0, norm);

    ncnn::Extractor ex = net_.create_extractor();
    ex.input("in0", in_pad);
    ncnn::Mat out0, out1, out2;
    ex.extract("out0", out0);
    ex.extract("out1", out1);
    ex.extract("out2", out2);

    std::vector<obj_confect> proposals;
    float anchor8_data[]   = {10,13, 16,30, 33,23};
    float anchor16_data[]  = {30,61, 62,45, 59,119};
    float anchor32_data[] = {116,90, 156,198, 373,326};

    ncnn::Mat anchors8  = ncnn::Mat(6, anchor8_data);
    ncnn::Mat anchors16 = ncnn::Mat(6, anchor16_data);
    ncnn::Mat anchors32 = ncnn::Mat(6, anchor32_data);

    generate_proposals(anchors8,  8,  out0, prob_thresh, proposals);
    generate_proposals(anchors16, 16, out1, prob_thresh, proposals);
    generate_proposals(anchors32, 32, out2, prob_thresh, proposals);

    // 按置信度排序
    std::sort(proposals.begin(), proposals.end(),
              [](const obj_confect& a, const obj_confect& b) { return a.prob > b.prob; });

    std::vector<int> picked;
    nms_sorted_bboxes(proposals, picked, nms_thresh);

    objects.clear();
    for (int idx : picked) objects.push_back(proposals[idx]);

    // 还原到原图坐标
    for (size_t i = 0; i < objects.size(); i++) {
        float x0 = (objects[i].rect.x - wpad/2) / scale;
        float y0 = (objects[i].rect.y - hpad/2) / scale;
        float x1 = (objects[i].rect.x + objects[i].rect.width  - wpad/2) / scale;
        float y1 = (objects[i].rect.y + objects[i].rect.height - hpad/2) / scale;
        x0 = std::max(0.f, std::min(x0, (float)bgr.cols - 1));
        y0 = std::max(0.f, std::min(y0, (float)bgr.rows - 1));
        x1 = std::max(0.f, std::min(x1, (float)bgr.cols - 1));
        y1 = std::max(0.f, std::min(y1, (float)bgr.rows - 1));
        objects[i].rect.x = x0;
        objects[i].rect.y = y0;
        objects[i].rect.width  = x1 - x0;
        objects[i].rect.height = y1 - y0;
    }
    return 0;
}

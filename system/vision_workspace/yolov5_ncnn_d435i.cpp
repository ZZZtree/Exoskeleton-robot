#include <opencv2/opencv.hpp>
#include <net.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <linux/videodev2.h>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <unordered_map>

// ==================== RTLinux/实时支持 ====================
// 支持两种模式：
// 1. Xenomai (需要安装 xenomai-dev 包)
// 2. 标准 Linux RT (SCHED_FIFO + mlockall, 默认)

#include <errno.h>
#include <time.h>

#define RT_PRIORITY_HIGH   85   // 高优先级
#define RT_PRIORITY_MED    70   // 中优先级

static bool g_rt_mode = false;

// 标准 Linux 实时模式初始化 (POSIX + SCHED_FIFO)
static int init_rt_mode(bool enable_rt) {
    if (!enable_rt) {
        printf("[NRT] 非实时模式运行\n");
        return 0;
    }

    printf("[RT] 初始化 Linux 实时环境...\n");

    // 锁定内存，防止换页
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        printf("[RT] 警告：mlockall 失败，%s\n", strerror(errno));
    }

    // 设置当前线程为实时优先级
    struct sched_param param;
    param.sched_priority = RT_PRIORITY_HIGH;

    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        printf("[RT] 警告：pthread_setschedparam 失败，%s\n", strerror(errno));
        printf("[RT]      需要 root 权限或 CAP_SYS_NICE capability\n");
        return -1;
    }

    g_rt_mode = true;
    printf("[RT] Linux 实时模式已启用 (SCHED_FIFO, 优先级 %d)\n", RT_PRIORITY_HIGH);
    return 0;
}

static void cleanup_rt_mode() {
    if (g_rt_mode) {
        printf("[RT] 实时任务已清理\n");
    }
}

// 设置线程为实时优先级
static int set_thread_rt_priority(int priority) {
    struct sched_param param;
    param.sched_priority = priority;

    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        printf("[RT] 警告：pthread_setschedparam 失败，%s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// =============================================

// ==================== 配置 ====================
#define BUFFER_COUNT 4
#define VIDEO_SCAN_START 0
#define VIDEO_SCAN_END 9

#define DUAL_STREAM_RGB_DETECT_INTERVAL 3
#define SINGLE_STREAM_DETECT_INTERVAL 3

static const int SUPPORTED_WIDTHS[] = {640, 848, 1280, 1920};
static const int SUPPORTED_HEIGHTS[] = {480, 480, 720, 1080};

#define YOLO_CONF_THRESH 0.40f
#define YOLO_NMS_THRESH 0.30f

#define CPU_MONITOR_INTERVAL_MS 1000
#define EPOLL_TIMEOUT_MS 33
#define FRAME_BUFFER_SIZE 2
// ==============================================

struct obj_confect
{
    cv::Rect_<float> rect;
    int label;
    float prob;
};

struct Trackobj_confect
{
    float cx, cy, w, h;
    int track_id;
    int label;
    float score;
    int lost_frames;
    float kf[8];
};

// CPU统计结构
struct CPUStats {
    std::deque<float> cpu_usage_history;
    std::mutex cpu_mutex;
    float avg_cpu = 0;
    float max_cpu = 0;
    float min_cpu = 100;
    std::atomic<int> thread_count{0};
    std::atomic<bool> should_stop{false};
    
    static float getSystemCPUUsage() {
        static long long last_idle = 0, last_total = 0;
        static std::mutex mtx;
        std::lock_guard<std::mutex> lock(mtx);
        
        FILE* fp = fopen("/proc/stat", "r");
        if (!fp) return -1;
        
        char buf[256];
        if (!fgets(buf, sizeof(buf), fp)) {
            fclose(fp);
            return -1;
        }
        fclose(fp);
        
        long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (sscanf(buf, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 8) {
            return -1;
        }
        
        long long total_idle = idle + iowait;
        long long total_non_idle = user + nice + system + irq + softirq + steal;
        long long total = total_idle + total_non_idle;
        
        long long total_diff = total - last_total;
        long long idle_diff = total_idle - last_idle;
        
        float cpu_percent = 0;
        if (total_diff > 0) {
            cpu_percent = 100.0f * (1.0f - ((float)idle_diff / total_diff));
        }
        
        last_idle = total_idle;
        last_total = total;
        
        return cpu_percent;
    }
    
    void update(float cpu_percent) {
        if (cpu_percent < 0) return;
        
        std::lock_guard<std::mutex> lock(cpu_mutex);
        cpu_usage_history.push_back(cpu_percent);
        if (cpu_usage_history.size() > 60) {
            cpu_usage_history.pop_front();
        }
        
        float sum = 0;
        for (auto c : cpu_usage_history) sum += c;
        avg_cpu = sum / cpu_usage_history.size();
        
        if (cpu_percent > max_cpu) max_cpu = cpu_percent;
        if (cpu_percent < min_cpu) min_cpu = cpu_percent;
    }
    
    void print_cpu_summary(const char* mode_name) {
        std::lock_guard<std::mutex> lock(cpu_mutex);
        printf("\n========== %s CPU使用统计 ==========\n", mode_name);
        printf("平均CPU使用率: %.1f%%\n", avg_cpu);
        printf("最高CPU使用率: %.1f%%\n", max_cpu);
        printf("最低CPU使用率: %.1f%%\n", min_cpu == 100 ? 0 : min_cpu);
        printf("活跃线程数: %d\n", thread_count.load());
        printf("=====================================\n");
    }
};

struct PerformanceStats {
    std::atomic<int> total_frames{0};
    std::atomic<int> detect_count{0};
    std::atomic<int> track_count{0};
    std::atomic<int> rgb_frames{0};
    std::atomic<int> ir_frames{0};
    std::atomic<int> dropped_frames{0};
    
    std::mutex latency_mutex;
    float total_detect_latency = 0;
    
    std::deque<float> fps_history;
    std::mutex history_mutex;
    
    float avg_fps = 0;
    float avg_detect_latency = 0;
    float max_fps = 0;
    float min_fps = 9999;
    
    CPUStats cpu_stats;
    
    void record_frame(bool is_detect = false, float latency_ms = 0) {
        total_frames++;
        track_count++;
        if (is_detect) {
            detect_count++;
            std::lock_guard<std::mutex> lock(latency_mutex);
            total_detect_latency += latency_ms;
        }
    }
    
    void record_rgb_frame() { rgb_frames++; }
    void record_ir_frame() { ir_frames++; }
    void record_dropped() { dropped_frames++; }
    
    void update_fps(float fps) {
        std::lock_guard<std::mutex> lock(history_mutex);
        fps_history.push_back(fps);
        if (fps_history.size() > 100) {
            fps_history.pop_front();
        }
        
        float sum = 0;
        for (auto f : fps_history) sum += f;
        avg_fps = sum / fps_history.size();
        
        if (fps > max_fps) max_fps = fps;
        if (fps < min_fps) min_fps = fps;
    }
    
    void print_summary(const char* mode_name) {
        printf("\n========== %s 性能统计 ==========\n", mode_name);
        printf("显示帧数: %d\n", total_frames.load());
        printf("RGB流帧数: %d\n", rgb_frames.load());
        printf("IR流帧数: %d\n", ir_frames.load());
        printf("丢帧数: %d\n", dropped_frames.load());
        printf("总数据流: %d (RGB+IR)\n", rgb_frames.load() + ir_frames.load());
        printf("检测次数: %d\n", detect_count.load());
        printf("跟踪更新: %d\n", track_count.load());
        printf("平均显示FPS: %.2f\n", avg_fps);
        printf("最高FPS: %.2f\n", max_fps);
        printf("最低FPS: %.2f\n", min_fps == 9999 ? 0 : min_fps);
        
        int dcount = detect_count.load();
        if (dcount > 0) {
            std::lock_guard<std::mutex> lock(latency_mutex);
            avg_detect_latency = total_detect_latency / dcount;
            printf("平均检测延迟: %.2f ms\n", avg_detect_latency);
        }
        printf("=====================================\n");
        
        cpu_stats.print_cpu_summary(mode_name);
    }
};

static const char* class_names[] = {
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

// ==================== ByteTrack ====================
class ByteTrack
{
public:
    std::vector<Trackobj_confect> tracks;
    int next_id = 1;
    const int max_lost = 30;
    const float iou_thresh = 0.30f;

    void predict_only()
    {
        for (auto& trk : tracks)
        {
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
    }

    std::vector<Trackobj_confect> update(const std::vector<obj_confect>& detections)
    {
        std::vector<Trackobj_confect> new_tracks;
        std::vector<bool> matched_det(detections.size(), false);
        std::vector<bool> matched_trk(tracks.size(), false);

        for (auto& trk : tracks)
        {
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

        for (int d = 0; d < (int)detections.size(); d++)
        {
            const auto& det = detections[d];
            float best_iou = 0;
            int best_t = -1;

            for (int t = 0; t < (int)tracks.size(); t++)
            {
                if (matched_trk[t]) continue;
                float iou_val = iou(det, tracks[t]);
                if (iou_val > best_iou && iou_val >= iou_thresh)
                {
                    best_iou = iou_val;
                    best_t = t;
                }
            }

            if (best_t >= 0)
            {
                auto& trk = tracks[best_t];
                trk.cx = det.rect.x + det.rect.width * 0.5f;
                trk.cy = det.rect.y + det.rect.height * 0.5f;
                trk.w  = det.rect.width;
                trk.h  = det.rect.height;
                trk.score = det.prob;
                trk.label = det.label;
                trk.lost_frames = 0;

                float alpha = 0.85f;
                trk.kf[4] = alpha * (trk.cx - trk.kf[0]) + (1-alpha)*trk.kf[4];
                trk.kf[5] = alpha * (trk.cy - trk.kf[1]) + (1-alpha)*trk.kf[5];
                trk.kf[6] = alpha * (trk.w  - trk.kf[2]) + (1-alpha)*trk.kf[6];
                trk.kf[7] = alpha * (trk.h  - trk.kf[3]) + (1-alpha)*trk.kf[7];
                trk.kf[0] = alpha * trk.cx + (1-alpha)*trk.kf[0];
                trk.kf[1] = alpha * trk.cy + (1-alpha)*trk.kf[1];
                trk.kf[2] = alpha * trk.w  + (1-alpha)*trk.kf[2];
                trk.kf[3] = alpha * trk.h  + (1-alpha)*trk.kf[3];

                matched_det[d] = true;
                matched_trk[best_t] = true;
            }
        }

        for (int t = 0; t < (int)tracks.size(); t++)
        {
            if (matched_trk[t] || tracks[t].lost_frames < max_lost)
                new_tracks.push_back(tracks[t]);
        }

        for (int d = 0; d < (int)detections.size(); d++)
        {
            if (!matched_det[d])
            {
                const auto& det = detections[d];
                Trackobj_confect tobj;
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

        tracks.swap(new_tracks);
        return tracks;
    }

private:
    float iou(const obj_confect& det, const Trackobj_confect& trk)
    {
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
};

// ==================== YOLOv5 NCNN ====================
static inline float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

static inline float intersection_area(const obj_confect& a, const obj_confect& b)
{
    cv::Rect_<float> inter = a.rect & b.rect;
    return inter.area();
}

static void qsort_descent_inplace(std::vector<obj_confect>& objects, int left, int right)
{
    int i = left, j = right;
    float p = objects[(left+right)/2].prob;
    while (i <= j)
    {
        while (objects[i].prob > p) i++;
        while (objects[j].prob < p) j--;
        if (i <= j) std::swap(objects[i++], objects[j--]);
    }
    if (left < j) qsort_descent_inplace(objects, left, j);
    if (i < right) qsort_descent_inplace(objects, i, right);
}

static void qsort_descent_inplace(std::vector<obj_confect>& objects)
{
    if (!objects.empty()) qsort_descent_inplace(objects, 0, objects.size()-1);
}

static void nms_sorted_bboxes(const std::vector<obj_confect>& objects, std::vector<int>& picked, float nms_threshold)
{
    picked.clear();
    int n = objects.size();
    std::vector<float> areas(n);
    for (int i=0; i<n; i++) areas[i] = objects[i].rect.area();
    
    for (int i=0; i<n; i++)
    {
        int keep = 1;
        for (int j : picked)
        {
            float inter = intersection_area(objects[i], objects[j]);
            float union_area = areas[i] + areas[j] - inter;
            if (union_area > 0) {
                float iou = inter / union_area;
                if (iou > nms_threshold) { keep = 0; break; }
            }
        }
        if (keep) picked.push_back(i);
    }
}

static void generate_proposals(const ncnn::Mat& anchors, int stride,
                                const ncnn::Mat& feat_blob, float prob_threshold, std::vector<obj_confect>& objects)
{
    int num_grid_x = feat_blob.w;
    int num_grid_y = feat_blob.h;
    int num_anchors = 3;
    int num_class = 80;

    for (int a=0; a<num_anchors; a++)
    {
        float aw = anchors[a*2+0];
        float ah = anchors[a*2+1];
        int c = a * 85;
        for (int y=0; y<num_grid_y; y++)
        {
            for (int x=0; x<num_grid_x; x++)
            {
                float dx = sigmoid(feat_blob.channel(c+0).row(y)[x]);
                float dy = sigmoid(feat_blob.channel(c+1).row(y)[x]);
                float dw = sigmoid(feat_blob.channel(c+2).row(y)[x]);
                float dh = sigmoid(feat_blob.channel(c+3).row(y)[x]);
                float obj_conf = sigmoid(feat_blob.channel(c+4).row(y)[x]);
                if (obj_conf < prob_threshold) continue;

                float cls_score = 0;
                int cls_id = 0;
                for (int k=0; k<num_class; k++)
                {
                    float s = sigmoid(feat_blob.channel(c+5+k).row(y)[x]);
                    if (s > cls_score) { cls_score = s; cls_id = k; }
                }
                float conf = obj_conf * cls_score;
                if (conf < prob_threshold) continue;

                float cx = (dx*2-0.5f + x) * stride;
                float cy = (dy*2-0.5f + y) * stride;
                float w  = powf(dw*2, 2) * aw;
                float h  = powf(dh*2, 2) * ah;

                obj_confect obj;
                obj.rect.x = cx - w*0.5f;
                obj.rect.y = cy - h*0.5f;
                obj.rect.width = w;
                obj.rect.height = h;
                obj.label = cls_id;
                obj.prob = conf;
                objects.push_back(obj);
            }
        }
    }
}

static int detect_yolov5(ncnn::Net& net, const cv::Mat& bgr, std::vector<obj_confect>& objects)
{
    const int target_size = 640;
    const float prob_thresh = YOLO_CONF_THRESH;
    const float nms_thresh = YOLO_NMS_THRESH;
    int w = bgr.cols, h = bgr.rows;

    float scale = 1.f;
    if (w > h) { scale = (float)target_size / w; w = target_size; h *= scale; }
    else       { scale = (float)target_size / h; h = target_size; w *= scale; }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR2RGB, bgr.cols, bgr.rows, w, h);
    int wpad = target_size - w;
    int hpad = target_size - h;
    ncnn::Mat in_pad;
    ncnn::copy_make_border(in, in_pad, hpad/2, hpad-hpad/2, wpad/2, wpad-wpad/2, ncnn::BORDER_CONSTANT, 114.f);

    const float norm[] = {1/255.f,1/255.f,1/255.f};
    in_pad.substract_mean_normalize(0, norm);

    ncnn::Extractor ex = net.create_extractor();
    ex.input("in0", in_pad);
    ncnn::Mat out0, out1, out2;
    ex.extract("out0", out0);
    ex.extract("out1", out1);
    ex.extract("out2", out2);

    std::vector<obj_confect> proposals;
    
    float anchor8_data[] = {10,13, 16,30, 33,23};
    float anchor16_data[] = {30,61, 62,45, 59,119};
    float anchor32_data[] = {116,90, 156,198, 373,326};
    
    ncnn::Mat anchors8 = ncnn::Mat(6, anchor8_data);
    ncnn::Mat anchors16 = ncnn::Mat(6, anchor16_data);
    ncnn::Mat anchors32 = ncnn::Mat(6, anchor32_data);

    generate_proposals(anchors8,  8,  out0, prob_thresh, proposals);
    generate_proposals(anchors16, 16, out1, prob_thresh, proposals);
    generate_proposals(anchors32, 32, out2, prob_thresh, proposals);

    qsort_descent_inplace(proposals);
    std::vector<int> picked;
    nms_sorted_bboxes(proposals, picked, nms_thresh);

    objects.clear();
    for (int idx : picked) objects.push_back(proposals[idx]);

    for (size_t i=0; i<objects.size(); i++)
    {
        float x0 = (objects[i].rect.x - wpad/2) / scale;
        float y0 = (objects[i].rect.y - hpad/2) / scale;
        float x1 = (objects[i].rect.x + objects[i].rect.width - wpad/2) / scale;
        float y1 = (objects[i].rect.y + objects[i].rect.height - hpad/2) / scale;
        
        x0 = std::max(0.f, std::min(x0, (float)bgr.cols-1));
        y0 = std::max(0.f, std::min(y0, (float)bgr.rows-1));
        x1 = std::max(0.f, std::min(x1, (float)bgr.cols-1));
        y1 = std::max(0.f, std::min(y1, (float)bgr.rows-1));
        objects[i].rect.x = x0;
        objects[i].rect.y = y0;
        objects[i].rect.width = x1 - x0;
        objects[i].rect.height = y1 - y0;
    }
    return 0;
}

// ==================== V4L2 封装 ====================
struct BufferInfo {
    void* addr;
    size_t length;
};

struct CameraConfig {
    int fd;
    BufferInfo bufs[BUFFER_COUNT];
    int width;
    int height;
    __u32 pixel_format;
    std::string device_path;
    std::string device_name;
    bool is_yuyv;
    bool is_rgb;
    bool is_bgr;
    bool is_grey;
    std::atomic<bool> should_stop{false};
    bool nonblock_mode = false;
};

static void print_fourcc(__u32 pixelformat, char* buf)
{
    buf[0] = pixelformat & 0xFF;
    buf[1] = (pixelformat >> 8) & 0xFF;
    buf[2] = (pixelformat >> 16) & 0xFF;
    buf[3] = (pixelformat >> 24) & 0xFF;
    buf[4] = '\0';
}

static bool try_format(int fd, int width, int height, __u32 pixelformat, 
                       int& out_width, int& out_height, __u32& out_pixelformat)
{
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) return false;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) return false;
    
    out_width = fmt.fmt.pix.width;
    out_height = fmt.fmt.pix.height;
    out_pixelformat = fmt.fmt.pix.pixelformat;
    return true;
}

static bool probe_device(const char* dev, std::string& out_name, std::vector<__u32>& out_formats)
{
    int fd = open(dev, O_RDWR);
    if (fd < 0) return false;
    
    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) { close(fd); return false; }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) { close(fd); return false; }
    
    out_name = (char*)cap.card;
    v4l2_fmtdesc fmtdesc{};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        out_formats.push_back(fmtdesc.pixelformat);
        fmtdesc.index++;
    }
    close(fd);
    return true;
}

static bool init_camera(const char* dev, CameraConfig& cam, bool nonblock = false)
{
    memset(&cam, 0, sizeof(cam));
    cam.fd = -1;
    cam.device_path = dev;
    cam.nonblock_mode = nonblock;
    
    int flags = O_RDWR;
    if (nonblock) flags |= O_NONBLOCK;
    
    cam.fd = open(dev, flags);
    if (cam.fd < 0) { perror("open"); printf("无法打开: %s\n", dev); return false; }

    v4l2_capability cap;
    if (ioctl(cam.fd, VIDIOC_QUERYCAP, &cap) < 0) { close(cam.fd); cam.fd = -1; return false; }
    cam.device_name = (char*)cap.card;
    printf("设备[%s]: %s\n", dev, cam.device_name.c_str());

    v4l2_fmtdesc fmtdesc{};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printf("  支持的格式: ");
    
    std::vector<__u32> supported_formats;
    while (ioctl(cam.fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        supported_formats.push_back(fmtdesc.pixelformat);
        char fourcc[5];
        print_fourcc(fmtdesc.pixelformat, fourcc);
        printf("%s ", fourcc);
        fmtdesc.index++;
    }
    printf("\n");

    if (supported_formats.empty()) { close(cam.fd); cam.fd = -1; return false; }

    __u32 try_formats[] = {
        V4L2_PIX_FMT_YUYV, v4l2_fourcc('R','G','B','8'), v4l2_fourcc('B','G','R','8'),
        V4L2_PIX_FMT_GREY, v4l2_fourcc('Y','8',' ',' '), v4l2_fourcc('U','Y','V','Y'), 0
    };

    bool format_set = false;
    for (int fi = 0; try_formats[fi] != 0 && !format_set; fi++) {
        bool format_supported = false;
        for (auto f : supported_formats) if (f == try_formats[fi]) { format_supported = true; break; }
        if (!format_supported) continue;

        for (size_t ri = 0; ri < sizeof(SUPPORTED_WIDTHS)/sizeof(int); ri++) {
            int w = SUPPORTED_WIDTHS[ri], h = SUPPORTED_HEIGHTS[ri];
            __u32 fmt_out;
            if (try_format(cam.fd, w, h, try_formats[fi], cam.width, cam.height, fmt_out)) {
                cam.pixel_format = fmt_out;
                cam.is_yuyv = (fmt_out == V4L2_PIX_FMT_YUYV) || (fmt_out == v4l2_fourcc('U','Y','V','Y'));
                cam.is_rgb = (fmt_out == v4l2_fourcc('R','G','B','8'));
                cam.is_bgr = (fmt_out == v4l2_fourcc('B','G','R','8'));
                cam.is_grey = (fmt_out == V4L2_PIX_FMT_GREY) || (fmt_out == v4l2_fourcc('Y','8',' ',' '));
                format_set = true;
                char fourcc[5];
                print_fourcc(fmt_out, fourcc);
                printf("  使用格式: %dx%d %s\n", cam.width, cam.height, fourcc);
                break;
            }
        }
    }
    
    if (!format_set) { close(cam.fd); cam.fd = -1; return false; }

    v4l2_requestbuffers req{};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam.fd, VIDIOC_REQBUFS, &req) < 0) { close(cam.fd); cam.fd = -1; return false; }

    for (int i=0; i<BUFFER_COUNT; i++)
    {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(cam.fd, VIDIOC_QUERYBUF, &buf) < 0) { close(cam.fd); cam.fd = -1; return false; }
        cam.bufs[i].addr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam.fd, buf.m.offset);
        cam.bufs[i].length = buf.length;
        if (cam.bufs[i].addr == MAP_FAILED) { close(cam.fd); cam.fd = -1; return false; }
        if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) { close(cam.fd); cam.fd = -1; return false; }
    }

    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam.fd, VIDIOC_STREAMON, &t) < 0) { close(cam.fd); cam.fd = -1; return false; }

    printf("  初始化成功 (%dx%d) %s\n", cam.width, cam.height, 
           nonblock ? "[非阻塞]" : "[阻塞]");
    return true;
}

static void cleanup_camera(CameraConfig& cam)
{
    if (cam.fd < 0) return;
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam.fd, VIDIOC_STREAMOFF, &t);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (cam.bufs[i].addr && cam.bufs[i].addr != MAP_FAILED) {
            munmap(cam.bufs[i].addr, cam.bufs[i].length);
            cam.bufs[i].addr = nullptr;
        }
    }
    close(cam.fd);
    cam.fd = -1;
}

static bool read_frame(CameraConfig& cam, cv::Mat& bgr)
{
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0) return false;

    if (buf.index >= BUFFER_COUNT) {
        ioctl(cam.fd, VIDIOC_QBUF, &buf);
        return false;
    }

    if (cam.is_yuyv) {
        cv::Mat yuyv(cam.height, cam.width, CV_8UC2, cam.bufs[buf.index].addr);
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
    } 
    else if (cam.is_rgb) {
        cv::Mat rgb(cam.height, cam.width, CV_8UC3, cam.bufs[buf.index].addr);
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    }
    else if (cam.is_bgr) {
        cv::Mat tmp(cam.height, cam.width, CV_8UC3, cam.bufs[buf.index].addr);
        tmp.copyTo(bgr);
    }
    else if (cam.is_grey) {
        cv::Mat grey(cam.height, cam.width, CV_8UC1, cam.bufs[buf.index].addr);
        cv::cvtColor(grey, bgr, cv::COLOR_GRAY2BGR);
    }
    else {
        ioctl(cam.fd, VIDIOC_QBUF, &buf);
        return false;
    }

    if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) return false;
    return true;
}

static bool read_frame_nonblock(CameraConfig& cam, cv::Mat& bgr)
{
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    int ret = ioctl(cam.fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        if (errno == EAGAIN) return false;
        return false;
    }

    if (buf.index >= BUFFER_COUNT) {
        ioctl(cam.fd, VIDIOC_QBUF, &buf);
        return false;
    }

    if (cam.is_yuyv) {
        cv::Mat yuyv(cam.height, cam.width, CV_8UC2, cam.bufs[buf.index].addr);
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
    } 
    else if (cam.is_rgb) {
        cv::Mat rgb(cam.height, cam.width, CV_8UC3, cam.bufs[buf.index].addr);
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    }
    else if (cam.is_bgr) {
        cv::Mat tmp(cam.height, cam.width, CV_8UC3, cam.bufs[buf.index].addr);
        tmp.copyTo(bgr);
    }
    else if (cam.is_grey) {
        cv::Mat grey(cam.height, cam.width, CV_8UC1, cam.bufs[buf.index].addr);
        cv::cvtColor(grey, bgr, cv::COLOR_GRAY2BGR);
    }
    else {
        ioctl(cam.fd, VIDIOC_QBUF, &buf);
        return false;
    }

    if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) return false;
    return true;
}

static void scan_cameras(std::vector<std::string>& rgb_devices, std::vector<std::string>& ir_devices)
{
    printf("扫描视频设备 %d-%d...\n", VIDEO_SCAN_START, VIDEO_SCAN_END);
    for (int i = VIDEO_SCAN_START; i <= VIDEO_SCAN_END; i++) {
        char dev_path[32];
        snprintf(dev_path, sizeof(dev_path), "/dev/video%d", i);
        std::string name;
        std::vector<__u32> formats;
        if (!probe_device(dev_path, name, formats)) continue;
        
        printf("  发现 %s: %s\n", dev_path, name.c_str());
        bool has_yuyv = false, has_rgb = false, has_grey = false;
        for (auto f : formats) {
            if (f == V4L2_PIX_FMT_YUYV) has_yuyv = true;
            if (f == v4l2_fourcc('R','G','B','8')) has_rgb = true;
            if (f == V4L2_PIX_FMT_GREY || f == v4l2_fourcc('Y','8',' ',' ')) has_grey = true;
        }
        if (has_yuyv || has_rgb) {
            printf("    -> 彩色摄像头候选\n");
            rgb_devices.push_back(dev_path);
        }
        else if (has_grey) {
            printf("    -> 红外/深度候选\n");
            ir_devices.push_back(dev_path);
        }
    }
    printf("扫描完成: 发现 %zu 个彩色设备, %zu 个红外设备\n", 
           rgb_devices.size(), ir_devices.size());
}

// ==================== Epoll V4L2 管理器 ====================
class V4L2EpollManager {
public:
    struct CameraContext {
        CameraConfig* cam;
        std::deque<cv::Mat> frame_queue;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        bool is_rgb;
    };

private:
    int epoll_fd = -1;
    std::unordered_map<int, CameraContext*> contexts;
    std::atomic<bool> running{false};
    std::thread event_thread;
    int wakeup_fd = -1;
    
public:
    bool init() {
        epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd < 0) {
            perror("epoll_create1");
            return false;
        }
        
        wakeup_fd = eventfd(0, EFD_NONBLOCK);
        if (wakeup_fd < 0) {
            perror("eventfd");
            close(epoll_fd);
            epoll_fd = -1;
            return false;
        }
        
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.ptr = nullptr;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wakeup_fd, &ev) < 0) {
            perror("epoll_ctl wakeup_fd");
            close(wakeup_fd);
            close(epoll_fd);
            wakeup_fd = -1;
            epoll_fd = -1;
            return false;
        }
        
        return true;
    }
    
    void cleanup() {
        stop();
        
        if (wakeup_fd >= 0) {
            close(wakeup_fd);
            wakeup_fd = -1;
        }
        
        for (auto& pair : contexts) {
            delete pair.second;
        }
        contexts.clear();
        
        if (epoll_fd >= 0) {
            close(epoll_fd);
            epoll_fd = -1;
        }
    }
    
    bool add_camera(CameraConfig& cam, bool is_rgb) {
        if (epoll_fd < 0) return false;
        
        int flags = fcntl(cam.fd, F_GETFL, 0);
        if (!(flags & O_NONBLOCK)) {
            fcntl(cam.fd, F_SETFL, flags | O_NONBLOCK);
        }
        
        CameraContext* ctx = new CameraContext();
        ctx->cam = &cam;
        ctx->is_rgb = is_rgb;
        contexts[cam.fd] = ctx;
        
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = ctx;
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cam.fd, &ev) < 0) {
            perror("epoll_ctl ADD");
            delete ctx;
            contexts.erase(cam.fd);
            return false;
        }
        
        printf("[Epoll] 添加摄像头 %s (fd=%d, %s)\n", 
               cam.device_name.c_str(), cam.fd, is_rgb ? "RGB" : "IR");
        return true;
    }
    
    bool start() {
        if (running.exchange(true)) return false;
        
        event_thread = std::thread(&V4L2EpollManager::event_loop, this);
        return true;
    }
    
    void stop() {
        if (!running.exchange(false)) return;
        
        if (wakeup_fd >= 0) {
            uint64_t one = 1;
            ssize_t ret = write(wakeup_fd, &one, sizeof(one));
            (void)ret;
        }
        
        if (event_thread.joinable()) {
            event_thread.join();
        }
    }
    
    bool get_frame(CameraConfig& cam, cv::Mat& frame, int timeout_ms = 0) {
        auto it = contexts.find(cam.fd);
        if (it == contexts.end()) return false;
        
        CameraContext* ctx = it->second;
        std::unique_lock<std::mutex> lock(ctx->queue_mutex);
        
        if (timeout_ms > 0) {
            ctx->queue_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
                return !ctx->frame_queue.empty() || !running;
            });
        }
        
        if (ctx->frame_queue.empty()) return false;
        
        frame = ctx->frame_queue.front();
        ctx->frame_queue.pop_front();
        return true;
    }
    
    bool try_get_frame(CameraConfig& cam, cv::Mat& frame) {
        return get_frame(cam, frame, 0);
    }
    
    size_t get_queue_size(CameraConfig& cam) {
        auto it = contexts.find(cam.fd);
        if (it == contexts.end()) return 0;
        
        std::lock_guard<std::mutex> lock(it->second->queue_mutex);
        return it->second->frame_queue.size();
    }
    
    void clear_queue(CameraConfig& cam) {
        auto it = contexts.find(cam.fd);
        if (it == contexts.end()) return;
        
        std::lock_guard<std::mutex> lock(it->second->queue_mutex);
        it->second->frame_queue.clear();
    }

private:
    void event_loop() {
        printf("[Epoll] 事件循环启动\n");
        
        struct epoll_event events[10];
        
        while (running) {
            int nfds = epoll_wait(epoll_fd, events, 10, 100);
            
            for (int i = 0; i < nfds; i++) {
                if (events[i].data.ptr == nullptr) {
                    if (events[i].data.fd == wakeup_fd) {
                        uint64_t val;
                        ssize_t ret = read(wakeup_fd, &val, sizeof(val));
                        (void)ret;
                    }
                    continue;
                }
                
                CameraContext* ctx = (CameraContext*)events[i].data.ptr;
                
                if (events[i].events & EPOLLIN) {
                    process_camera_data(ctx);
                }
            }
        }
        
        printf("[Epoll] 事件循环结束\n");
    }
    
    void process_camera_data(CameraContext* ctx) {
        CameraConfig* cam = ctx->cam;
        cv::Mat frame;
        
        while (running) {
            if (!read_frame_nonblock(*cam, frame)) {
                break;
            }
            
            std::lock_guard<std::mutex> lock(ctx->queue_mutex);
            
            if (ctx->frame_queue.size() < FRAME_BUFFER_SIZE) {
                ctx->frame_queue.push_back(frame.clone());
                ctx->queue_cv.notify_one();
            } else {
                ctx->frame_queue.pop_front();
                ctx->frame_queue.push_back(frame.clone());
            }
        }
    }
};

// ==================== CPU监控线程 ====================
void cpu_monitor_thread(std::atomic<bool>& running, PerformanceStats& stats, int interval_ms) {
    while (running) {
        float cpu = CPUStats::getSystemCPUUsage();
        if (cpu >= 0) {
            stats.cpu_stats.update(cpu);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

// ==================== 渲染辅助函数 ====================
static void render_frame(cv::Mat& display, 
                        const std::vector<Trackobj_confect>& tracks,
                        const std::vector<obj_confect>& detections,
                        PerformanceStats& stats,
                        const char* mode_name,
                        int rgb_count, int ir_count,
                        bool is_detect_frame)
{
    for (const auto& obj : detections) {
        cv::rectangle(display, obj.rect, cv::Scalar(0, 255, 0), 1);
        char txt[32];
        snprintf(txt, sizeof(txt), "%s:%.2f", class_names[obj.label], obj.prob);
        cv::putText(display, txt, cv::Point(obj.rect.x, obj.rect.y - 3),
                   cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
    }
    
    for (const auto& t : tracks) {
        cv::Rect r(t.cx - t.w*0.5f, t.cy - t.h*0.5f, t.w, t.h);
        cv::rectangle(display, r, cv::Scalar(255, 0, 255), 2);
        char txt[32];
        snprintf(txt, sizeof(txt), "ID:%d", t.track_id);
        cv::putText(display, txt, cv::Point(r.x, r.y - 5),
                   cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 0, 255), 2);
    }

    int info_height = 160;
    cv::rectangle(display, cv::Rect(0, 0, 700, info_height), cv::Scalar(0,0,0), -1);
    char info[128];
    
    snprintf(info, sizeof(info), "%s | FPS:%.1f | T:%zu | CPU:%.1f%%", 
            mode_name, stats.avg_fps, tracks.size(), stats.cpu_stats.avg_cpu);
    cv::putText(display, info, cv::Point(10, 30), 
               cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    
    snprintf(info, sizeof(info), "RGB:%d IR:%d Dropped:%d", 
            rgb_count, ir_count, stats.dropped_frames.load());
    cv::putText(display, info, cv::Point(10, 65), 
               cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 1);
    
    snprintf(info, sizeof(info), "Dets:%zu | %s", 
            detections.size(), is_detect_frame ? "DETECT" : "PREDICT");
    cv::putText(display, info, cv::Point(10, 95), 
               cv::FONT_HERSHEY_SIMPLEX, 0.6, 
               is_detect_frame ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255), 1);
    
    snprintf(info, sizeof(info), "Threads: Epoll + Detect + Main + CPU-mon");
    cv::putText(display, info, cv::Point(10, 125), 
               cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(200, 200, 200), 1);
               
    snprintf(info, sizeof(info), "Queue: RGB[%d] IR[%d]", 
            rgb_count, ir_count);
    cv::putText(display, info, cv::Point(10, 155), 
               cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(200, 200, 200), 1);
}

// ==================== 前向声明 ====================
int run_dual_stream(ncnn::Net& yolov5, CameraConfig& cam_rgb, CameraConfig& cam_ir, PerformanceStats& stats);

// ==================== Epoll 优化双流模式 ====================
int run_dual_stream_epoll(ncnn::Net& yolov5, CameraConfig& cam_rgb, CameraConfig& cam_ir, PerformanceStats& stats)
{
    printf("\n========== 启动 Epoll 优化双流模式 ==========\n");
    printf("特点：事件驱动，零等待，边缘触发，队列流控\n");
    
    V4L2EpollManager epoll_mgr;
    if (!epoll_mgr.init()) {
        printf("Epoll 初始化失败\n");
        return -1;
    }
    
    cleanup_camera(cam_rgb);
    cleanup_camera(cam_ir);
    
    if (!init_camera(cam_rgb.device_path.c_str(), cam_rgb, true)) {
        printf("RGB摄像头非阻塞模式初始化失败\n");
        return -1;
    }
    if (!init_camera(cam_ir.device_path.c_str(), cam_ir, true)) {
        printf("IR摄像头非阻塞模式初始化失败\n");
        cleanup_camera(cam_rgb);
        return -1;
    }
    
    epoll_mgr.add_camera(cam_rgb, true);
    epoll_mgr.add_camera(cam_ir, false);
    epoll_mgr.start();
    
    ByteTrack tracker;
    cv::namedWindow("EPOLL DUAL STREAM", cv::WINDOW_NORMAL);
    
    std::vector<obj_confect> latest_detections;
    std::mutex detection_mutex;
    std::atomic<int> rgb_frame_count{0};
    std::atomic<int> ir_frame_count{0};
    std::atomic<bool> running{true};
    
    // ========== 修复：启动CPU监控线程 ==========
    std::thread cpu_thread(cpu_monitor_thread, std::ref(running), std::ref(stats), CPU_MONITOR_INTERVAL_MS);
    stats.cpu_stats.thread_count.store(4);
    // ============================================
    
    std::thread detect_thread([&]() {
        cv::Mat frame;
        std::vector<obj_confect> local_dets;
        int local_count = 0;
        
        while (running) {
            if (!epoll_mgr.get_frame(cam_rgb, frame, 100)) {
                continue;
            }
            
            local_count++;
            rgb_frame_count.store(local_count);
            stats.record_rgb_frame();
            
            if (local_count % DUAL_STREAM_RGB_DETECT_INTERVAL == 0) {
                auto t0 = std::chrono::steady_clock::now();
                detect_yolov5(yolov5, frame, local_dets);
                auto t1 = std::chrono::steady_clock::now();
                float latency = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                
                {
                    std::lock_guard<std::mutex> lock(detection_mutex);
                    latest_detections = local_dets;
                }
                stats.record_frame(true, latency);
            }
        }
    });
    
    printf("开始 Epoll 主循环，按 ESC 退出...\n");
    
    int display_frame_count = 0;
    auto fps_time = std::chrono::steady_clock::now();
    cv::Mat ir_frame;
    
    while (running) {
        bool has_ir = epoll_mgr.try_get_frame(cam_ir, ir_frame);
        
        if (!has_ir) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        ir_frame_count++;
        stats.record_ir_frame();
        
        std::vector<obj_confect> current_dets;
        {
            std::lock_guard<std::mutex> lock(detection_mutex);
            current_dets = latest_detections;
        }
        
        auto tracks = tracker.update(current_dets);
        stats.record_frame();
        
        display_frame_count++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - fps_time).count();
        
        if (elapsed_us >= 500000) {
            float fps = display_frame_count * 1000000.0f / elapsed_us;
            stats.update_fps(fps);
            display_frame_count = 0;
            fps_time = now;
        }
        
        cv::Mat display = ir_frame.clone();
        bool is_detect_frame = (rgb_frame_count.load() % DUAL_STREAM_RGB_DETECT_INTERVAL == 0);
        render_frame(display, tracks, current_dets, stats, "EPOLL DUAL", 
                    rgb_frame_count.load(), ir_frame_count.load(), is_detect_frame);
        
        cv::imshow("EPOLL DUAL STREAM", display);
        
        int key = cv::waitKey(1);
        if (key == 27) {
            running = false;
        }
    }
    
    // ========== 修复：等待CPU监控线程结束 ==========
    detect_thread.join();
    cpu_thread.join();
    // ============================================
    
    epoll_mgr.stop();
    epoll_mgr.cleanup();
    cv::destroyWindow("EPOLL DUAL STREAM");
    
    return 0;
}

// ==================== 传统双流模式 ====================
int run_dual_stream(ncnn::Net& yolov5, CameraConfig& cam_rgb, CameraConfig& cam_ir, PerformanceStats& stats)
{
    printf("\n========== 启动传统双流模式 (RGB+IR) ==========\n");
    printf("RGB: 低频检测(每%d帧) | IR: 高频跟踪(每帧)\n", DUAL_STREAM_RGB_DETECT_INTERVAL);
    
    ByteTrack tracker;
    cv::namedWindow("DUAL STREAM", cv::WINDOW_NORMAL);

    std::vector<obj_confect> rgb_detections;
    std::mutex detection_mutex;
    std::atomic<bool> running(true);
    
    std::thread cpu_thread(cpu_monitor_thread, std::ref(running), std::ref(stats), CPU_MONITOR_INTERVAL_MS);
    stats.cpu_stats.thread_count.store(5);
    
    std::atomic<int> rgb_frame_count{0};
    std::atomic<int> ir_frame_count{0};
    int display_frame_count = 0;
    auto fps_time = std::chrono::steady_clock::now();

    std::thread detect_thread([&]() {
        cv::Mat rgb_frame;
        int local_count = 0;
        while (running) {
            auto t0 = std::chrono::steady_clock::now();
            
            if (!read_frame(cam_rgb, rgb_frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            
            local_count++;
            rgb_frame_count.store(local_count);
            stats.record_rgb_frame();
            
            if (local_count % DUAL_STREAM_RGB_DETECT_INTERVAL == 0) {
                std::vector<obj_confect> dets;
                detect_yolov5(yolov5, rgb_frame, dets);
                
                auto t1 = std::chrono::steady_clock::now();
                float latency = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                
                std::lock_guard<std::mutex> lock(detection_mutex);
                rgb_detections = dets;
                stats.record_frame(true, latency);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    printf("开始双流主循环，按 ESC 退出...\n");
    
    while (running)
    {
        cv::Mat ir_frame;
        
        if (!read_frame(cam_ir, ir_frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        int current_ir = ir_frame_count.fetch_add(1) + 1;
        stats.record_ir_frame();

        std::vector<obj_confect> current_dets;
        {
            std::lock_guard<std::mutex> lock(detection_mutex);
            current_dets = rgb_detections;
        }
        
        auto tracks = tracker.update(current_dets);
        stats.record_frame();

        display_frame_count++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - fps_time).count();
        
        float display_fps = 0;
        if (elapsed_us >= 500000) {
            display_fps = display_frame_count * 1000000.0f / elapsed_us;
            stats.update_fps(display_fps);
            display_frame_count = 0;
            fps_time = now;
        }

        float current_cpu = stats.cpu_stats.avg_cpu;

        cv::Mat display = ir_frame.clone();
        render_frame(display, tracks, current_dets, stats, "DUAL", 
                    rgb_frame_count.load(), current_ir, 
                    (rgb_frame_count.load() % DUAL_STREAM_RGB_DETECT_INTERVAL == 0));

        cv::imshow("DUAL STREAM", display);
        int key = cv::waitKey(1);
        if (key == 27) running = false;
    }

    running = false;
    detect_thread.join();
    cpu_thread.join();
    cv::destroyWindow("DUAL STREAM");
    return 0;
}

// ==================== 单流模式 ====================
int run_single_stream(ncnn::Net& yolov5, CameraConfig& cam, PerformanceStats& stats)
{
    printf("\n========== 启动单流模式 (单一摄像头) ==========\n");
    printf("每%d帧检测一次，中间帧只做跟踪预测\n", SINGLE_STREAM_DETECT_INTERVAL);
    
    ByteTrack tracker;
    cv::namedWindow("SINGLE STREAM", cv::WINDOW_NORMAL);

    std::vector<obj_confect> detections;
    int frame_idx = 0;
    int display_frame_count = 0;
    auto fps_time = std::chrono::steady_clock::now();
    
    std::atomic<bool> running(true);
    std::thread cpu_thread(cpu_monitor_thread, std::ref(running), std::ref(stats), CPU_MONITOR_INTERVAL_MS);
    stats.cpu_stats.thread_count.store(3);

    printf("开始单流主循环，按 ESC 退出...\n");

    while (running)
    {
        cv::Mat frame;
        
        if (!read_frame(cam, frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        frame_idx++;

        bool did_detect = false;
        float detect_latency = 0;
        
        if (frame_idx % SINGLE_STREAM_DETECT_INTERVAL == 0) {
            auto t0 = std::chrono::steady_clock::now();
            detections.clear();
            detect_yolov5(yolov5, frame, detections);
            auto t1 = std::chrono::steady_clock::now();
            detect_latency = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            did_detect = true;
            stats.record_frame(true, detect_latency);
        } else {
            tracker.predict_only();
            stats.record_frame(false, 0);
        }

        auto tracks = tracker.update(detections);

        display_frame_count++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - fps_time).count();
        
        if (elapsed_us >= 500000) {
            float fps = display_frame_count * 1000000.0f / elapsed_us;
            stats.update_fps(fps);
            display_frame_count = 0;
            fps_time = now;
        }

        float current_cpu = stats.cpu_stats.avg_cpu;

        cv::Mat display = frame.clone();
        render_frame(display, tracks, detections, stats, "SINGLE", 
                    frame_idx, 0, did_detect);

        cv::imshow("SINGLE STREAM", display);
        int key = cv::waitKey(1);
        if (key == 27) running = false;
    }

    running = false;
    cpu_thread.join();
    cv::destroyWindow("SINGLE STREAM");
    return 0;
}

// ==================== main ====================
int main(int argc, char* argv[])
{
    // 检查是否启用 RT 模式
    bool enable_rt = false;
    
    printf("[DEBUG] 命令行参数数量: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("[DEBUG] argv[%d] = %s\n", i, argv[i]);
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rt") == 0) {
            enable_rt = true;
            printf("[DEBUG] 检测到 --rt 参数\n");
        }
    }

    printf("目标检测跟踪系统 - Epoll优化版本 (Linux 5.15+)\n");
    printf("=============================================\n\n");

    // 初始化 RTLinux/Xenomai 实时模式
    if (init_rt_mode(enable_rt) != 0) {
        printf("警告：RT 模式初始化失败，继续使用普通模式\n");
    }

    ncnn::Net yolov5;
    yolov5.opt.num_threads = std::thread::hardware_concurrency();
    yolov5.opt.use_fp16_packed = false;
    yolov5.opt.use_fp16_storage = false;
    yolov5.opt.use_bf16_storage = false;
    
    if (yolov5.load_param("yolov5s.ncnn.param") || yolov5.load_model("yolov5s.ncnn.bin"))
    {
        printf("模型加载失败\n");
        return -1;
    }
    printf("模型加载成功，使用 %d 线程\n", yolov5.opt.num_threads);
    printf("CPU核心数: %d\n\n", std::thread::hardware_concurrency());

    std::vector<std::string> rgb_devices, ir_devices;
    scan_cameras(rgb_devices, ir_devices);
    
    if (rgb_devices.empty()) {
        printf("错误：没有找到可用的彩色摄像头\n");
        return -1;
    }

    printf("\n【优化说明】\n");
    printf("1. 传统模式: 阻塞IO + sleep轮询\n");
    printf("2. Epoll模式: 事件驱动 + 边缘触发 + 队列流控\n");
    printf("3. 电机控制: 正弦运动 (使用 --motor 启用)\n\n");

    printf("选择运行模式：\n");
    printf("1. 单流模式\n");
    printf("2. 传统双流模式\n");
    printf("3. Epoll优化双流模式\n");
    printf("4. 对比模式 (传统 vs Epoll)\n");
    printf("选择 (1/2/3/4): ");
    
    int mode = 1;
    if (scanf("%d", &mode) != 1) {
        mode = 1;
    }
    getchar();

    CameraConfig cam_rgb{}, cam_ir{};
    PerformanceStats stats_single, stats_dual, stats_epoll;

    if (mode == 1) {
        if (!init_camera(rgb_devices[0].c_str(), cam_rgb)) return -1;
        run_single_stream(yolov5, cam_rgb, stats_single);
        cleanup_camera(cam_rgb);
        stats_single.print_summary("单流模式");
        return 0;
    }

    if (mode == 2) {
        if (ir_devices.empty()) {
            printf("错误：需要IR摄像头\n");
            return -1;
        }
        if (!init_camera(rgb_devices[0].c_str(), cam_rgb)) return -1;
        if (!init_camera(ir_devices[0].c_str(), cam_ir)) {
            cleanup_camera(cam_rgb);
            return -1;
        }
        run_dual_stream(yolov5, cam_rgb, cam_ir, stats_dual);
        cleanup_camera(cam_rgb);
        cleanup_camera(cam_ir);
        stats_dual.print_summary("传统双流模式");
        return 0;
    }

    if (mode == 3) {
        if (ir_devices.empty()) {
            printf("错误：需要IR摄像头\n");
            return -1;
        }
        if (!init_camera(rgb_devices[0].c_str(), cam_rgb)) return -1;
        if (!init_camera(ir_devices[0].c_str(), cam_ir)) {
            cleanup_camera(cam_rgb);
            return -1;
        }
        run_dual_stream_epoll(yolov5, cam_rgb, cam_ir, stats_epoll);
        cleanup_camera(cam_rgb);
        cleanup_camera(cam_ir);
        stats_epoll.print_summary("Epoll优化双流模式");
        return 0;
    }

    if (mode == 4) {
        if (ir_devices.empty()) {
            printf("错误：需要IR摄像头\n");
            return -1;
        }
        
        printf("\n===== 第一阶段：传统双流模式 =====\n");
        if (!init_camera(rgb_devices[0].c_str(), cam_rgb)) return -1;
        if (!init_camera(ir_devices[0].c_str(), cam_ir)) {
            cleanup_camera(cam_rgb);
            return -1;
        }
        run_dual_stream(yolov5, cam_rgb, cam_ir, stats_dual);
        cleanup_camera(cam_rgb);
        cleanup_camera(cam_ir);
        
        printf("\n按 Enter 开始 Epoll 优化模式...");
        getchar();
        
        printf("\n===== 第二阶段：Epoll优化双流模式 =====\n");
        if (!init_camera(rgb_devices[0].c_str(), cam_rgb)) return -1;
        if (!init_camera(ir_devices[0].c_str(), cam_ir)) {
            cleanup_camera(cam_rgb);
            return -1;
        }
        run_dual_stream_epoll(yolov5, cam_rgb, cam_ir, stats_epoll);
        cleanup_camera(cam_rgb);
        cleanup_camera(cam_ir);
        
        printf("\n\n");
        printf("##################################################\n");
        printf("#           传统 vs Epoll 性能对比报告          #\n");
        printf("##################################################\n");
        stats_dual.print_summary("传统双流模式");
        stats_epoll.print_summary("Epoll优化双流模式");
        
        printf("\n========== 关键指标对比 ==========\n");
        printf("平均FPS:     传统 %.1f  vs  Epoll %.1f  (%.1f%%)\n",
               stats_dual.avg_fps, stats_epoll.avg_fps,
               (stats_epoll.avg_fps/stats_dual.avg_fps - 1)*100);
        printf("平均CPU:     传统 %.1f%% vs  Epoll %.1f%% (%.1f%%)\n",
               stats_dual.cpu_stats.avg_cpu, stats_epoll.cpu_stats.avg_cpu,
               (stats_epoll.cpu_stats.avg_cpu/stats_dual.cpu_stats.avg_cpu - 1)*100);
        printf("丢帧数:      传统 %d    vs  Epoll %d\n",
               stats_dual.dropped_frames.load(), stats_epoll.dropped_frames.load());
        printf("检测延迟:    传统 %.1fms vs  Epoll %.1fms\n",
               stats_dual.avg_detect_latency, stats_epoll.avg_detect_latency);
        printf("##################################################\n");
    }

    cleanup_rt_mode();
    return 0;
}
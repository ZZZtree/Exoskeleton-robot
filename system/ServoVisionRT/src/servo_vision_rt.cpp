/**
 * servo_vision_rt.cpp
 *
 * Real-time integrated system: YOLOv5 vision detection + motor servo control
 * Running on RT-Linux (PREEMPT_RT kernel 5.15.113-rt64)
 *
 * Thread architecture (2-core / 4-thread system):
 *   Core 0:  Motor_Servo_Thread  (SCHED_FIFO, prio=99) — 1 ms hard real-time
 *   Core 0:  Epoll_Camera_Thread (SCHED_FIFO, prio=80) — camera event handling
 *   Core 1:  YOLO_Detect_Thread  (SCHED_OTHER)          — NCNN inference
 *   Core 1:  Display_Thread      (SCHED_OTHER)           — OpenCV imshow
 *
 * Data flow:
 *   D435i RGB ──▶ Epoll ──▶ FrameQueue ──┐
 *                                         ▼
 *                                   YOLO Detect (lock-free ping-pong)
 *                                         │
 *   D435i IR  ──▶ Epoll ──▶ FrameQueue ──┴──▶ Motor_Servo
 *                                                    │
 *                                              SetAxisPosition
 */

// ── System headers ────────────────────────────────────────────────────────────
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <vector>
#include <deque>

// ── HYYRobot SDK ─────────────────────────────────────────────────────────────
#include "HYYRobotInterface.h"
#include "DeviceDriver/device_timer.h"

// ── OpenCV + NCNN ───────────────────────────────────────────────────────────
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include "ncnn/net.h"
#include "ncnn/mat.h"

// ── V4L2 ────────────────────────────────────────────────────────────────────
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/epoll.h>

// Suppress "ignoring return value" warnings for intentionally unchecked calls
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"

// ── RT utilities ─────────────────────────────────────────────────────────────
#include "rt_utils.h"

using namespace HYYRobotBase;
using namespace rt;

#include <signal.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// CLI / runtime flags
// ============================================================================
static bool g_nogui = false;  // headless mode: skip all OpenCV GUI calls

// Defined here (not later) so the SIGINT handler can reach it.
static std::atomic<bool> g_running{true};

static void sigint_handler(int) {
    g_running = false;
}

static void sigsegv_handler(int sig) {
    void* buf[32];
    int n = backtrace(buf, 32);
    fprintf(stderr, "\n[CRASH] SIGSEGV received — backtrace:\n");
    backtrace_symbols_fd(buf, n, STDERR_FILENO);
    fprintf(stderr, "[CRASH] sig=%d  backtrace_size=%d\n", sig, n);
    _Exit(1);
}

// ============================================================================
// Detection data structure (mirrors the original vision project's struct)
// ============================================================================
struct ObjRect {
    float x, y, width, height;
    float area() const { return width * height; }
};

struct Detection {
    ObjRect rect;
    int label;
    float prob;
};

// Detection frame — container for a full detection result at one moment
struct DetectionFrame {
    std::vector<Detection> detections;
    int frame_counter = 0;
};

// ============================================================================
// Configuration
// ============================================================================
namespace Config {
    // Motor — SDK总线周期(5ms)，通过 userTimer() 对齐 EtherCAT 时钟
    constexpr int     MOTOR_PRIORITY    = 99;
    constexpr int     MOTOR_CPU         = 1;   // isolated core (isolcpus=1,3)

    // Vision threads
    constexpr int     EPOLL_PRIORITY    = 80;
    constexpr int     EPOLL_CPU         = 3;   // isolated core (other than motor)
    constexpr int     YOLO_CPU          = 0;
    constexpr int     DISPLAY_CPU       = 1;

    // Vision
    constexpr int     BUFFER_COUNT      = 4;
    constexpr int     FRAME_BUFFER_SIZE = 2;
    constexpr int     DETECT_INTERVAL   = 3;   // detect every N RGB frames
    constexpr int     TARGET_SIZE        = 640;
    constexpr float   YOLO_CONF_THRESH  = 0.01f;  // TEMP: lowered for debug
    constexpr float   YOLO_NMS_THRESH   = 0.30f;
    constexpr int     RGB_SCAN_START    = 0;
    constexpr int     RGB_SCAN_END      = 9;

    static const int  SUPPORTED_WIDTHS[]  = {640, 848, 1280, 1920};
    static const int  SUPPORTED_HEIGHTS[] = {480, 480,  720, 1080};
}

// ============================================================================
// Lock-free ping-pong detection buffer
// ============================================================================
static DetectionFrame g_det_buf[2];
static int g_det_write = 0;
static int g_det_read  = 0;
static std::mutex g_det_swap_mutex;
static std::atomic<bool> g_det_ready{false};

// ============================================================================
// V4L2 camera structure
// ============================================================================
struct CameraInfo {
    int fd = -1;
    int width = 0, height = 0;
    __u32 pixel_format = 0;
    bool is_yuyv = false, is_rgb = false, is_bgr = false, is_grey = false;
    std::string path;
    std::string name;
    std::atomic<bool> should_stop{false};
};

// Per-camera V4L2 buffer state: each CameraInfo owns its own buffers so that
// multiple cameras do not clobber each other's mmap addresses.
struct CamBuf {
    std::vector<void*> addrs;   // mmap'd buffer addresses
    std::vector<size_t> lens;  // corresponding lengths
    bool init = false;
};

static std::unordered_map<int, CamBuf> s_cam_bufs;  // key = fd
static std::mutex s_cam_bufs_mutex;

static void close_camera(CameraInfo& cam) {
    if (cam.fd < 0) return;
    int fd = cam.fd;  // save fd before invalidating it
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam.fd, VIDIOC_STREAMOFF, &t);
    close(cam.fd);
    cam.fd = -1;

    std::lock_guard<std::mutex> lock(s_cam_bufs_mutex);
    auto it = s_cam_bufs.find(fd);
    if (it != s_cam_bufs.end()) {
        for (size_t i = 0; i < it->second.addrs.size(); i++) {
            munmap(it->second.addrs[i], it->second.lens[i]);
        }
        s_cam_bufs.erase(it);
    }
}

struct BufInfo { void* addr = nullptr; size_t length = 0; };

// ============================================================================
// YOLOv5 NCNN inference
// ============================================================================
static inline float _sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

static void qsort_descent(std::vector<Detection>& objs, int l, int r) {
    if (l >= r || objs.empty()) return;
    int i = l, j = r;
    float p = objs[(l + r) >> 1].prob;
    while (i <= j) {
        while (objs[i].prob > p) i++;
        while (objs[j].prob < p) j--;
        if (i <= j) std::swap(objs[i++], objs[j--]);
    }
    if (l < j) qsort_descent(objs, l, j);
    if (i < r) qsort_descent(objs, i, r);
}

static void nms_sorted_bboxes(const std::vector<Detection>& objs,
                               std::vector<int>& picked, float nms_thr) {
    picked.clear();
    const int n = (int)objs.size();
    if (n == 0) return;
    std::vector<float> areas(n);
    for (int i = 0; i < n; i++) areas[i] = objs[i].rect.area();
    for (int i = 0; i < n; i++) {
        bool keep = true;
        for (int j : picked) {
            float ix1 = std::max(objs[i].rect.x, objs[j].rect.x);
            float iy1 = std::max(objs[i].rect.y, objs[j].rect.y);
            float ix2 = std::min(objs[i].rect.x + objs[i].rect.width,
                                  objs[j].rect.x + objs[j].rect.width);
            float iy2 = std::min(objs[i].rect.y + objs[i].rect.height,
                                  objs[j].rect.y + objs[j].rect.height);
            float iw = std::max(0.f, ix2 - ix1);
            float ih = std::max(0.f, iy2 - iy1);
            float inter = iw * ih;
            if (inter > 0 && inter / (areas[i] + areas[j] - inter) > nms_thr) {
                keep = false; break;
            }
        }
        if (keep) picked.push_back(i);
    }
}

static void generate_proposals(const float* anchors, int stride,
                                 const ncnn::Mat& feat,
                                 float prob_thr,
                                 std::vector<Detection>& out) {
    const int gw = feat.w, gh = feat.h, gc = feat.c;
    static int dbg_stride = 0;
    if (dbg_stride == 0) {
        dbg_stride = stride;
        fprintf(stderr, "[YOLO]  DBG feat: stride=%d  w=%d h=%d c=%d\n", stride, gw, gh, gc);
    }
    if (gw <= 0 || gh <= 0 || gc < 85) {
        fprintf(stderr, "[YOLO] WARNING: invalid feat dims %dx%dx%d\n", gw, gh, gc);
        return;
    }
    for (int a = 0; a < 3; a++) {
        float aw = anchors[a * 2], ah = anchors[a * 2 + 1];
        int c = a * 85;
        if (c + 84 >= gc) continue;
        for (int y = 0; y < gh; y++) {
            for (int x = 0; x < gw; x++) {
                float obj_conf = _sigmoid(feat.channel(c + 4).row(y)[x]);
                if (obj_conf < prob_thr) continue;

                int cls_id = 0; float cls_score = 0;
                for (int k = 0; k < 80 && (c + 5 + k) < gc; k++) {
                    float s = _sigmoid(feat.channel(c + 5 + k).row(y)[x]);
                    if (s > cls_score) { cls_score = s; cls_id = k; }
                }
                float conf = obj_conf * cls_score;
                if (conf < prob_thr) continue;

                float dx = _sigmoid(feat.channel(c + 0).row(y)[x]);
                float dy = _sigmoid(feat.channel(c + 1).row(y)[x]);
                float dw = _sigmoid(feat.channel(c + 2).row(y)[x]);
                float dh = _sigmoid(feat.channel(c + 3).row(y)[x]);

                float cx = (dx * 2 - 0.5f + x) * stride;
                float cy = (dy * 2 - 0.5f + y) * stride;
                float bw = powf(dw * 2, 2) * aw;
                float bh = powf(dh * 2, 2) * ah;

                Detection obj;
                obj.rect.x = cx - bw * 0.5f;
                obj.rect.y = cy - bh * 0.5f;
                obj.rect.width  = bw;
                obj.rect.height = bh;
                obj.label = cls_id;
                obj.prob  = conf;
                out.push_back(obj);
            }
        }
    }
}

static int detect_yolov5(ncnn::Net& net, const cv::Mat& bgr,
                          std::vector<Detection>& objects) {
    const float prob_thr = Config::YOLO_CONF_THRESH;
    const float nms_thr  = Config::YOLO_NMS_THRESH;
    int w = bgr.cols, h = bgr.rows;
    float scale = 1.f;
    if (w > h) { scale = (float)Config::TARGET_SIZE / w; w = Config::TARGET_SIZE; h = (int)(h * scale); }
    else       { scale = (float)Config::TARGET_SIZE / h; h = Config::TARGET_SIZE; w = (int)(w * scale); }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(
        bgr.data, ncnn::Mat::PIXEL_BGR2RGB, bgr.cols, bgr.rows, w, h);
    int wpad = Config::TARGET_SIZE - w;
    int hpad = Config::TARGET_SIZE - h;
    ncnn::Mat in_pad;
    ncnn::copy_make_border(in, in_pad,
        hpad / 2, hpad - hpad / 2, wpad / 2, wpad - wpad / 2,
        ncnn::BORDER_CONSTANT, 114.f);
    const float norm[3] = {1.f/255.f, 1.f/255.f, 1.f/255.f};
    in_pad.substract_mean_normalize(0, norm);

    ncnn::Extractor ex = net.create_extractor();
    ex.input("in0", in_pad);
    ncnn::Mat out0, out1, out2;
    ex.extract("out0", out0);
    ex.extract("out1", out1);
    ex.extract("out2", out2);

    std::vector<Detection> proposals;
    float anchor8[]  = {10,13, 16,30, 33,23};
    float anchor16[] = {30,61, 62,45, 59,119};
    float anchor32[] = {116,90, 156,198, 373,326};
    generate_proposals(anchor8,  8,  out0, prob_thr, proposals);
    generate_proposals(anchor16, 16, out1, prob_thr, proposals);
    generate_proposals(anchor32, 32, out2, prob_thr, proposals);

    qsort_descent(proposals, 0, (int)proposals.size() - 1);
    std::vector<int> picked;
    nms_sorted_bboxes(proposals, picked, nms_thr);

    objects.clear();
    for (int idx : picked) objects.push_back(proposals[idx]);

    for (auto& o : objects) {
        float x0 = (o.rect.x - wpad / 2.f) / scale;
        float y0 = (o.rect.y - hpad / 2.f) / scale;
        float x1 = (o.rect.x + o.rect.width - wpad / 2.f) / scale;
        float y1 = (o.rect.y + o.rect.height - hpad / 2.f) / scale;
        o.rect.x      = std::max(0.f, std::min(x0, (float)bgr.cols - 1));
        o.rect.y      = std::max(0.f, std::min(y0, (float)bgr.rows - 1));
        o.rect.width  = std::max(0.f, std::min(x1, (float)bgr.cols - 1) - o.rect.x);
        o.rect.height = std::max(0.f, std::min(y1, (float)bgr.rows - 1) - o.rect.y);
    }
    return 0;
}

// ============================================================================
// COCO 80 class names
// ============================================================================
static const char* coco_names[] = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light",
    "fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow",
    "elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
    "skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard",
    "tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple",
    "sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair","couch",
    "potted plant","bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone",
    "microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear",
    "hair drier","toothbrush"
};

// ============================================================================
// V4L2 camera helpers
// ============================================================================
static bool _try_fmt_one(int fd, int w, int h, __u32 fmt,
                          int& out_w, int& out_h, __u32& out_fmt) {
    v4l2_format f{};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    f.fmt.pix.width = (unsigned)w;
    f.fmt.pix.height = (unsigned)h;
    f.fmt.pix.pixelformat = fmt;
    f.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &f) < 0) return false;
    if (ioctl(fd, VIDIOC_G_FMT, &f) < 0) return false;
    out_w = (int)f.fmt.pix.width;
    out_h = (int)f.fmt.pix.height;
    out_fmt = f.fmt.pix.pixelformat;
    return true;
}

static bool init_camera(const char* dev, CameraInfo& cam, bool nonblock) {
    int flags = O_RDWR | (nonblock ? O_NONBLOCK : 0);
    cam.fd = open(dev, flags);
    if (cam.fd < 0) { perror("open"); return false; }

    v4l2_capability cap;
    if (ioctl(cam.fd, VIDIOC_QUERYCAP, &cap) < 0) { close(cam.fd); cam.fd = -1; return false; }
    cam.name = (char*)cap.card;
    cam.path  = dev;

    fprintf(stderr, "[V4L2] Device %s: %s\n", dev, cam.name.c_str());

    v4l2_fmtdesc fd{};
    fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    std::vector<__u32> supported;
    while (ioctl(cam.fd, VIDIOC_ENUM_FMT, &fd) == 0) {
        supported.push_back(fd.pixelformat);
        fd.index++;
    }

    __u32 try_fmt_list[] = {
        V4L2_PIX_FMT_YUYV,
        v4l2_fourcc('R','G','B','8'),
        v4l2_fourcc('B','G','R','8'),
        V4L2_PIX_FMT_GREY,
        0
    };
    bool ok = false;
    for (int fi = 0; try_fmt_list[fi]; fi++) {
        bool supported_fmt = false;
        for (auto sf : supported) if (sf == try_fmt_list[fi]) { supported_fmt = true; break; }
        if (!supported_fmt) continue;
        for (size_t ri = 0; ri < sizeof(Config::SUPPORTED_WIDTHS)/sizeof(int); ri++) {
            if (_try_fmt_one(cam.fd, Config::SUPPORTED_WIDTHS[ri],
                               Config::SUPPORTED_HEIGHTS[ri],
                               try_fmt_list[fi],
                               cam.width, cam.height, cam.pixel_format)) {
                cam.is_yuyv = (cam.pixel_format == V4L2_PIX_FMT_YUYV);
                cam.is_rgb  = (cam.pixel_format == v4l2_fourcc('R','G','B','8'));
                cam.is_bgr  = (cam.pixel_format == v4l2_fourcc('B','G','R','8'));
                cam.is_grey = (cam.pixel_format == V4L2_PIX_FMT_GREY);
                char fourcc[5] = {
                    (char)(cam.pixel_format&0xff),
                    (char)((cam.pixel_format>>8)&0xff),
                    (char)((cam.pixel_format>>16)&0xff), 0 };
                fprintf(stderr, "[V4L2]   Format: %dx%d %s\n", cam.width, cam.height, fourcc);
                ok = true; break;
            }
        }
        if (ok) break;
    }

    if (!ok) { close(cam.fd); cam.fd = -1; return false; }

    v4l2_requestbuffers req{};
    req.count  = Config::BUFFER_COUNT;
    req.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory  = V4L2_MEMORY_MMAP;
    if (ioctl(cam.fd, VIDIOC_REQBUFS, &req) < 0) { close(cam.fd); cam.fd = -1; return false; }

    static BufInfo s_bufs[Config::BUFFER_COUNT];
    for (int i = 0; i < Config::BUFFER_COUNT; i++) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(cam.fd, VIDIOC_QUERYBUF, &buf) < 0) { close(cam.fd); cam.fd = -1; return false; }
        s_bufs[i].addr   = mmap(nullptr, buf.length, PROT_READ|PROT_WRITE,
                                  MAP_SHARED, cam.fd, buf.m.offset);
        s_bufs[i].length = buf.length;
        if (s_bufs[i].addr == MAP_FAILED) { close(cam.fd); cam.fd = -1; return false; }
        ioctl(cam.fd, VIDIOC_QBUF, &buf);
    }

    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam.fd, VIDIOC_STREAMON, &t);
    return true;
}

// Read one frame in non-blocking mode
static bool read_frame_nb(CameraInfo& cam, cv::Mat& out) {
    CamBuf* cb = nullptr;
    bool needs_init = false;

    // Fetch or create the per-fd buffer state under lock; keep lock held for init
    {
        std::lock_guard<std::mutex> lock(s_cam_bufs_mutex);
        cb = &s_cam_bufs[cam.fd];
        needs_init = !cb->init;
    }

    if (needs_init) {
        std::lock_guard<std::mutex> lock(s_cam_bufs_mutex);
        if (!cb->init) {  // double-check (another thread may have finished)
            for (int i = 0; i < Config::BUFFER_COUNT; i++) {
                v4l2_buffer buf{};
                buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index  = i;
                ioctl(cam.fd, VIDIOC_QUERYBUF, &buf);
                void* addr = mmap(nullptr, buf.length, PROT_READ|PROT_WRITE,
                                   MAP_SHARED, cam.fd, buf.m.offset);
                if (addr == MAP_FAILED) {
                    // Clean up any partial buffers before erasing
                    for (size_t j = 0; j < cb->addrs.size(); j++)
                        munmap(cb->addrs[j], cb->lens[j]);
                    s_cam_bufs.erase(cam.fd);
                    return false;
                }
                cb->addrs.push_back(addr);
                cb->lens.push_back(buf.length);
                ioctl(cam.fd, VIDIOC_QBUF, &buf);
            }
            cb->init = true;
        }
    }

    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return false;
        return false;
    }
    if ((unsigned)buf.index >= cb->addrs.size()) {
        ioctl(cam.fd, VIDIOC_QBUF, &buf); return false;
    }

    void* addr = cb->addrs[buf.index];
    if (cam.is_yuyv) {
        cv::Mat yuyv(cam.height, cam.width, CV_8UC2, addr);
        cv::cvtColor(yuyv, out, cv::COLOR_YUV2BGR_YUYV);
    } else if (cam.is_rgb) {
        cv::Mat rgb(cam.height, cam.width, CV_8UC3, addr);
        cv::cvtColor(rgb, out, cv::COLOR_RGB2BGR);
    } else if (cam.is_bgr) {
        out = cv::Mat(cam.height, cam.width, CV_8UC3, addr).clone();
    } else if (cam.is_grey) {
        cv::Mat grey(cam.height, cam.width, CV_8UC1, addr);
        cv::cvtColor(grey, out, cv::COLOR_GRAY2BGR);
    } else {
        ioctl(cam.fd, VIDIOC_QBUF, &buf); return false;
    }
    ioctl(cam.fd, VIDIOC_QBUF, &buf);
    return true;
}

// ============================================================================
// Global state
// ============================================================================
struct RobotContext {
    const char* robot_name = nullptr;
    int axis_ID = 1;
    double pos_base = 0;
    double control_cycle = 0;
    int dof = 0;
    double joint[10];
} g_robot;

struct VisionContext {
    CameraInfo cam_rgb;
    CameraInfo cam_ir;
    std::deque<cv::Mat> rgb_queue;
    std::deque<cv::Mat> ir_queue;
    std::mutex rgb_q_mutex;
    std::mutex ir_q_mutex;
    std::condition_variable rgb_q_cv;
    std::condition_variable ir_q_cv;
} g_vision;

// ============================================================================
// Thread 1: Motor servo control  (SCHED_FIFO, prio=99, core 1)
// ============================================================================
static void* motor_servo_thread(void*) {
    pthread_t me = pthread_self();
    rt::lock_memory();
    rt::set_realtime_thread(me, Config::MOTOR_PRIORITY);

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(Config::MOTOR_CPU, &cpus);
    rt::set_cpu_affinity(me, cpus);

    fprintf(stderr, "[MOTOR] Started: SCHED_FIFO prio=%d core=%d (bus-aligned via userTimer)\n",
            Config::MOTOR_PRIORITY, Config::MOTOR_CPU);

    const char* robot = g_robot.robot_name;
    const int axis = g_robot.axis_ID;

    // Print initial joint positions
    GetGroupPosition(robot, g_robot.joint);
    fprintf(stderr, "[MOTOR] Initial joints: ");
    for (int i = 0; i < g_robot.dof; i++) fprintf(stderr, "%.3f ", g_robot.joint[i]);
    fprintf(stderr, "\n");

    // Sinusoidal reference parameters
    const double A = 0.5;   // amplitude (rad)
    const double f = 0.1;   // frequency (Hz)
    double pos_target = 0;
    int cycle_count = 0;

    // SDK timer —对齐 EtherCAT 总线周期（5ms），不能再快了
    RTimer timer;
    initUserTimer(&timer, 0, 1);   // 1× 总线周期

    // Pre-initialize EtherCAT on this core
    SetAxisPosition(robot, g_robot.pos_base, axis);

    // Track miss count for diagnostics (userTimerE returns 1 on boundary)
    int miss_count = 0;

    while (g_running) {
        userTimer(&timer);  // ← 精确对齐 EtherCAT 总线周期

        // --- Compute sinusoidal position reference ---
        pos_target = A * cos(2 * M_PI * f * (cycle_count * g_robot.control_cycle)) + g_robot.pos_base;

        // --- Reactive vision-guided offset (lock-free read) ---
        if (g_det_ready.load(std::memory_order_acquire)) {
            for (const auto& d : g_det_buf[g_det_read].detections) {
                if (d.label == 0) {  // "person"
                    float cx = d.rect.x + d.rect.width * 0.5f;
                    float img_cx = (cx / 640.f) * 2.f - 1.f;
                    pos_target += img_cx * 0.02;
                    break;
                }
            }
        }

        // --- Write position command to motor drive ---
        SetAxisPosition(robot, pos_target, axis);

        // --- Swap detection buffer every 4 cycles (~20ms at 5ms period) ---
        if ((cycle_count & 0x3) == 0) {
            std::lock_guard<std::mutex> lock(g_det_swap_mutex);
            g_det_read = 1 - g_det_write;
            g_det_buf[g_det_read].detections.clear();
            g_det_ready.store(false, std::memory_order_release);
        }

        // Periodic diagnostic log (every 1000 cycles ≈ 5 seconds)
        if (cycle_count > 0 && cycle_count % 1000 == 0) {
            fprintf(stderr, "[MOTOR] cycles=%d  t=%.3fs  pos=%.4f  miss=%d\n",
                    cycle_count, cycle_count * g_robot.control_cycle,
                    pos_target, miss_count);
        }

        cycle_count++;
    }

    fprintf(stderr, "[MOTOR] Thread exiting\n");
    return nullptr;
}

// ============================================================================
// Thread 2: Epoll camera event handler  (SCHED_FIFO, prio=80, core 0)
// ============================================================================
static void* epoll_camera_thread(void*) {
    pthread_t me = pthread_self();
    rt::lock_memory();
    rt::set_realtime_thread(me, Config::EPOLL_PRIORITY);

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(Config::EPOLL_CPU, &cpus);
    rt::set_cpu_affinity(me, cpus);

    fprintf(stderr, "[EPOLL] Started: SCHED_FIFO prio=%d core=%d\n",
            Config::EPOLL_PRIORITY, Config::EPOLL_CPU);

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) { perror("epoll_create1"); return nullptr; }

    int wakeup_fd = eventfd(0, EFD_NONBLOCK);
    struct epoll_event wakeup_ev{};
    wakeup_ev.events = EPOLLIN;
    wakeup_ev.data.u32 = 0;  // wakeup fd
    epoll_ctl(epfd, EPOLL_CTL_ADD, wakeup_fd, &wakeup_ev);

    // Register cameras
    CameraInfo* cameras[2] = { &g_vision.cam_rgb, &g_vision.cam_ir };
    for (int i = 0; i < 2; i++) {
        if (cameras[i]->fd < 0) continue;
        int flags = fcntl(cameras[i]->fd, F_GETFL);
        fcntl(cameras[i]->fd, F_SETFL, flags | O_NONBLOCK);
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.u32 = (unsigned)(i + 1);  // 1=RGB, 2=IR
        epoll_ctl(epfd, EPOLL_CTL_ADD, cameras[i]->fd, &ev);
    }

    struct epoll_event events[4];
    while (g_running) {
        int nfds = epoll_wait(epfd, events, 4, 100);
        if (nfds <= 0) continue;

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.u32 == 0) {  // wakeup fd
                __attribute__((unused)) uint64_t val;
                (void)::read(wakeup_fd, &val, sizeof(val));
                continue;
            }

            bool is_rgb = (events[i].data.u32 == 1);
            CameraInfo* cam = is_rgb ? &g_vision.cam_rgb : &g_vision.cam_ir;
            std::deque<cv::Mat>* queue = is_rgb ? &g_vision.rgb_queue : &g_vision.ir_queue;
            std::mutex* qmutex = is_rgb ? &g_vision.rgb_q_mutex : &g_vision.ir_q_mutex;
            std::condition_variable* qcv = is_rgb ? &g_vision.rgb_q_cv : &g_vision.ir_q_cv;

            cv::Mat frame;
            while (read_frame_nb(*cam, frame)) {
                std::lock_guard<std::mutex> lock(*qmutex);
                if ((int)queue->size() >= Config::FRAME_BUFFER_SIZE) {
                    queue->pop_front();
                }
                queue->push_back(frame.clone());
                qcv->notify_one();
            }
        }
    }

    close(wakeup_fd);
    close(epfd);
    fprintf(stderr, "[EPOLL] Thread exiting\n");
    return nullptr;
}

// ============================================================================
// Thread 3: YOLO detection  (SCHED_OTHER, core 1)
// ============================================================================
static void* yolo_detect_thread(void* arg) {
    ncnn::Net* net = static_cast<ncnn::Net*>(arg);

    // Lock memory to prevent page faults during inference (mmap'd NCNN model data)
    // mlockall is safe in non-RT threads; it prevents unexpected latency spikes.
    mlockall(MCL_CURRENT | MCL_FUTURE);

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(Config::YOLO_CPU, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus);

    fprintf(stderr, "[YOLO]  Started: SCHED_OTHER core=%d\n", Config::YOLO_CPU);

    cv::Mat rgb_frame;
    int frame_idx = 0;
    fprintf(stderr, "[YOLO]  Waiting for first RGB frame...\n");

    while (g_running) {
        // Wait for RGB frame
        {
            std::unique_lock<std::mutex> lock(g_vision.rgb_q_mutex);
            g_vision.rgb_q_cv.wait_for(lock, std::chrono::milliseconds(33), [&] {
                return !g_vision.rgb_queue.empty() || !g_running;
            });
            if (!g_running) break;
            if (g_vision.rgb_queue.empty()) continue;
            rgb_frame = g_vision.rgb_queue.front().clone();
            g_vision.rgb_queue.pop_front();
        }
        fprintf(stderr, "[YOLO]  Got frame #%d\n", ++frame_idx);

        // Debug: print first frame stats
        static bool dbg_printed = false;
        if (!dbg_printed && !rgb_frame.empty()) {
            dbg_printed = true;
            cv::Scalar mean = cv::mean(rgb_frame);
            fprintf(stderr, "[YOLO]  DBG frame: cols=%d rows=%d type=%d "
                    "mean=(%.1f,%.1f,%.1f)\n",
                    rgb_frame.cols, rgb_frame.rows, rgb_frame.type(),
                    mean[0], mean[1], mean[2]);
        }

        if (frame_idx % Config::DETECT_INTERVAL != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        auto t0 = std::chrono::steady_clock::now();

        std::vector<Detection> dets;
        detect_yolov5(*net, rgb_frame, dets);

        auto t1 = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        fprintf(stderr, "[YOLO]  #%-4d  objects=%-2zu  latency=%-4lld ms\n",
                frame_idx, dets.size(), (long long)latency);

        // Write to next ping-pong slot
        int next_write = 1 - g_det_write;
        {
            std::lock_guard<std::mutex> lock(g_det_swap_mutex);
            g_det_buf[next_write].detections = std::move(dets);
            g_det_buf[next_write].frame_counter = frame_idx;
            g_det_ready.store(true, std::memory_order_release);
            g_det_write = next_write;
        }
    }

    fprintf(stderr, "[YOLO]  Thread exiting\n");
    return nullptr;
}

// ============================================================================
// Thread 4: Display  (SCHED_OTHER, core 1)
// ============================================================================
static void* display_thread(void*) {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(Config::DISPLAY_CPU, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus);

    fprintf(stderr, "[DISP]  Started: SCHED_OTHER core=%d\n", Config::DISPLAY_CPU);

    std::vector<Detection> current_dets;
    int ir_count = 0;

    while (g_running) {
        cv::Mat ir_frame;
        {
            std::unique_lock<std::mutex> lock(g_vision.ir_q_mutex);
            g_vision.ir_q_cv.wait_for(lock, std::chrono::milliseconds(33), [&] {
                return !g_vision.ir_queue.empty() || !g_running;
            });
            if (!g_running) break;
            if (g_vision.ir_queue.empty()) continue;
            ir_frame = g_vision.ir_queue.front().clone();
            g_vision.ir_queue.pop_front();
        }
        ir_count++;

        {
            std::lock_guard<std::mutex> lock(g_det_swap_mutex);
            current_dets = g_det_buf[g_det_read].detections;
        }

        // Render
        cv::Mat display = ir_frame.clone();

        // Draw detection boxes
        for (const auto& d : current_dets) {
            cv::rectangle(display,
                cv::Rect((int)d.rect.x, (int)d.rect.y,
                         (int)d.rect.width, (int)d.rect.height),
                cv::Scalar(0, 255, 0), 1);
            char txt[64];
            snprintf(txt, sizeof(txt), "%s:%.2f", coco_names[d.label], d.prob);
            cv::putText(display, txt, cv::Point((int)d.rect.x, (int)d.rect.y - 3),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
        }

        // HUD overlay
        cv::rectangle(display, cv::Rect(0, 0, 560, 110), cv::Scalar(0, 0, 0), -1);
        char info[128];

        snprintf(info, sizeof(info),
                 "RT Servo+Vision | IR:%d | Dets:%zu | press ESC to quit", ir_count, current_dets.size());
        cv::putText(display, info, cv::Point(10, 25),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);

        snprintf(info, sizeof(info),
                 "Motor: 1ms SCHED_FIFO(99) core%d  |  Vision: SCHED_OTHER core%d",
                 Config::MOTOR_CPU, Config::YOLO_CPU);
        cv::putText(display, info, cv::Point(10, 50),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(200, 200, 200), 1);

        snprintf(info, sizeof(info),
                 "YOLO detect every %d RGB frames | Memory locked (mlockall)",
                 Config::DETECT_INTERVAL);
        cv::putText(display, info, cv::Point(10, 75),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(200, 200, 200), 1);

        snprintf(info, sizeof(info),
                 "Visual reactive: person detected -> axis offset");
        cv::putText(display, info, cv::Point(10, 100),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 255), 1);

        cv::imshow("RT Servo + Vision", display);
        int key = cv::waitKey(1);
        if (key == 27) {
            g_running = false;
            break;
        }
    }

    cv::destroyWindow("RT Servo + Vision");
    fprintf(stderr, "[DISP]  Thread exiting\n");
    return nullptr;
}

// ============================================================================
// Scan cameras and find RGB / IR devices
// ============================================================================
static void scan_cameras(std::string& rgb_dev, std::string& ir_dev) {
    fprintf(stderr, "[SCAN] Scanning /dev/video%d-%d...\n", Config::RGB_SCAN_START, Config::RGB_SCAN_END);
    for (int i = Config::RGB_SCAN_START; i <= Config::RGB_SCAN_END; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) { close(fd); continue; }

        v4l2_fmtdesc fd_desc{};
        fd_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        std::vector<__u32> fmts;
        while (ioctl(fd, VIDIOC_ENUM_FMT, &fd_desc) == 0) {
            fmts.push_back(fd_desc.pixelformat);
            fd_desc.index++;
        }

        bool has_color = false, has_grey = false;
        for (auto f : fmts) {
            if (f == V4L2_PIX_FMT_YUYV || f == v4l2_fourcc('R','G','B','8')) has_color = true;
            if (f == V4L2_PIX_FMT_GREY) has_grey = true;
        }

        if (has_color && rgb_dev.empty()) {
            rgb_dev = path;
            fprintf(stderr, "[SCAN]   RGB: %s (%s)\n", path, (char*)cap.card);
        } else if (has_grey && ir_dev.empty()) {
            ir_dev = path;
            fprintf(stderr, "[SCAN]   IR:  %s (%s)\n", path, (char*)cap.card);
        }
        close(fd);
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    fprintf(stderr, "============================================================\n");
    fprintf(stderr, "  RT Servo + Vision System  (RT-Linux PREEMPT_RT)\n");
    fprintf(stderr, "  Kernel: %s\n", "5.15.113-rt64 (5.15.113-rt64)");
    fprintf(stderr, "  Cores: %d  |  Motor: SCHED_FIFO(99)  |  Vision: SCHED_OTHER\n",
            (int)sysconf(_SC_NPROCESSORS_CONF));
    fprintf(stderr, "============================================================\n\n");

    // Catch SIGINT and SIGTERM so Ctrl+C cleanly shuts down threads
    struct sigaction sa{};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // restart interrupted syscalls (e.g. clock_nanosleep)
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    struct sigaction sa_segv{};
    sa_segv.sa_handler = sigsegv_handler;
    sigemptyset(&sa_segv.sa_mask);
    sa_segv.sa_flags = SA_RESTART;
    sigaction(SIGSEGV, &sa_segv, nullptr);
    sigaction(SIGABRT, &sa_segv, nullptr);

    // ── Robot initialization ────────────────────────────────────────────────
    HYYRobotBase::command_arg arg;

    // Merge user-provided args with --path and --iscopy true defaults.
    // When --iscopy is NOT set, the library initializes teach servers
    // (ports 6666-6668), which fails if another HYYRobotMain instance is
    // already running and owns those ports.  Passing --iscopy true
    // (exactly as HYYRobotMain does) tells the library this is a copy
    // process and skips teach-server startup — the same fix applied in
    // robot_control.cpp: arg_c="--path ... --iscopy true".
    bool has_path = false, has_iscopy = false, has_nogui = false;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--path", 6) == 0) has_path = true;
        if (strncmp(argv[i], "--iscopy", 8) == 0) has_iscopy = true;
        if (strncmp(argv[i], "--nogui", 7) == 0) has_nogui = true;
    }
    if (has_nogui) g_nogui = true;

    char merged_arg[1024];
    merged_arg[0] = '\0';
    for (int i = 1; i < argc; i++) {
        // --nogui is handled locally; skip it so SDK parser doesn't reject it
        if (strncmp(argv[i], "--nogui", 7) == 0) continue;
        if (i > 1) strncat(merged_arg, " ", sizeof(merged_arg) - strlen(merged_arg) - 1);
        strncat(merged_arg, argv[i], sizeof(merged_arg) - strlen(merged_arg) - 1);
    }
    if (!has_path) {
        strncat(merged_arg, " --path /home/robot/Work/system/robot_config",
                sizeof(merged_arg) - strlen(merged_arg) - 1);
    }
    if (!has_iscopy) {
        strncat(merged_arg, " --iscopy true",
                sizeof(merged_arg) - strlen(merged_arg) - 1);
    }

    fprintf(stderr, "[MAIN] Using args: %s\n", merged_arg);
    int err = commandLineParser1(merged_arg, &arg);
    if (err != 0) {
        fprintf(stderr, "[MAIN] commandLineParser failed: %d\n", err);
        return -1;
    }

    err = HYYRobotBase::system_initialize(&arg);
    if (err != 0) { fprintf(stderr, "[MAIN] system_initialize failed: %d\n", err); return err; }

    const char* device_name = get_deviceName(0, nullptr);
    g_robot.robot_name = get_name_robot_device(device_name, 0);
    g_robot.control_cycle = get_control_cycle(device_name);
    g_robot.dof = get_group_dof(g_robot.robot_name);
    GetGroupPosition(g_robot.robot_name, g_robot.joint);
    g_robot.pos_base = g_robot.joint[0];

    group_power_off(g_robot.robot_name);
    sleep(1);
    axis_power_on(g_robot.robot_name, g_robot.axis_ID);

    fprintf(stderr, "[MAIN] Robot: %s  DOF=%d  cycle=%.3f ms  base=%.4f rad\n",
            g_robot.robot_name, g_robot.dof,
            g_robot.control_cycle * 1000, g_robot.pos_base);

    // ── Load NCNN model ────────────────────────────────────────────────────
    ncnn::Net yolov5;
    yolov5.opt.num_threads = 1;
    yolov5.opt.use_fp16_packed = false;
    yolov5.opt.use_fp16_storage = false;
    yolov5.opt.use_bf16_storage = false;

    fprintf(stderr, "[MAIN] Loading YOLOv5 NCNN model...\n");
    if (yolov5.load_param("yolov5s.ncnn.param") != 0 ||
        yolov5.load_model("yolov5s.ncnn.bin") != 0) {
        fprintf(stderr, "[MAIN] FAILED — check yolov5s.ncnn.{param,bin} in working dir\n");
        HYYRobotBase::DevicePoweroff();
        return -1;
    }
    fprintf(stderr, "[MAIN] NCNN model loaded (threads=%d)\n", yolov5.opt.num_threads);

    // Warm-up inference: trigger all mmap page faults before RT threads start.
    // Without this, the first few inferences cause page faults that can corrupt
    // the rt::lock_memory() state or trigger segfaults when YOLO + motor threads
    // race on page fault handling.
    {
        fprintf(stderr, "[MAIN] Warming up YOLO inference...\n");
        cv::Mat dummy(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
        std::vector<Detection> dummy_dets;
        fprintf(stderr, "[MAIN]   calling detect_yolov5...\n");
        int warmup_err = detect_yolov5(yolov5, dummy, dummy_dets);
        if (warmup_err != 0) {
            fprintf(stderr, "[MAIN] FATAL: detect_yolov5 warm-up failed with code %d\n", warmup_err);
            HYYRobotBase::DevicePoweroff();
            return -1;
        }
        fprintf(stderr, "[MAIN]   detect_yolov5 returned OK (%zu objects)\n", dummy_dets.size());
        fprintf(stderr, "[MAIN] Warm-up done\n");
    }

    // ── Scan cameras ───────────────────────────────────────────────────────
    std::string rgb_dev, ir_dev;
    scan_cameras(rgb_dev, ir_dev);

    if (rgb_dev.empty()) {
        fprintf(stderr, "[MAIN] WARNING: No RGB camera found.\n");
        fprintf(stderr, "[MAIN]   Option: 1=continue without RGB  2=exit: ");
        int opt = 1;
        (void)scanf("%d", &opt);
        if (opt == 2) {
            HYYRobotBase::DevicePoweroff();
            return -1;
        }
    }

    // ── Open cameras ──────────────────────────────────────────────────────
    if (!rgb_dev.empty() && !init_camera(rgb_dev.c_str(), g_vision.cam_rgb, true))
        fprintf(stderr, "[MAIN] RGB camera init failed\n");
    if (!ir_dev.empty() && !init_camera(ir_dev.c_str(), g_vision.cam_ir, true))
        fprintf(stderr, "[MAIN] IR camera init failed\n");

    // ── Lock memory before RT threads ────────────────────────────────────
    rt::lock_memory();
    fprintf(stderr, "[MAIN] Memory locked (mlockall MCL_CURRENT|MCL_FUTURE)\n\n");

    // ── Launch threads (priority order) ─────────────────────────────────
    pthread_t motor_thr, epoll_thr, yolo_thr, display_thr;
    display_thr = 0;

    pthread_create(&motor_thr, nullptr, motor_servo_thread, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    pthread_create(&epoll_thr, nullptr, epoll_camera_thread, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    pthread_create(&yolo_thr, nullptr, yolo_detect_thread, &yolov5);

    if (!g_nogui) {
        // Must be called from main thread, BEFORE any RT threads start.
        // GTK backend fails if namedWindow is called from a non-main thread
        // or after SCHED_FIFO threads have been launched.
        cv::namedWindow("RT Servo + Vision", cv::WINDOW_NORMAL);
        pthread_create(&display_thr, nullptr, display_thread, nullptr);
    }

    fprintf(stderr, "[MAIN] All threads launched.\n");
    if (g_nogui) {
        fprintf(stderr, "[MAIN] Headless mode (--nogui): no display.  Send SIGINT to stop.\n\n");
    } else {
        fprintf(stderr, "[MAIN] Press ESC in window to stop.\n\n");
    }

    // ── Wait for threads ─────────────────────────────────────────────────
    if (g_nogui) {
        // In headless mode, spin until Ctrl+C (SIGINT) which sets g_running = false
        pthread_join(yolo_thr, nullptr);
    } else {
        pthread_join(display_thr, nullptr);
        g_running = false;
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    // motor_thr and epoll_thr are still running (g_running is still true here
    // in GUI mode, but they will be joined below after g_running = false above).
    // In nogui mode, yolo_thr is already joined above.
    if (g_nogui) {
        // motor and epoll threads are still running; join them now
        pthread_join(motor_thr, nullptr);
        pthread_join(epoll_thr, nullptr);
    } else {
        pthread_join(motor_thr, nullptr);
        pthread_join(epoll_thr, nullptr);
        pthread_join(yolo_thr, nullptr);  // already done in GUI path above
    }

    close_camera(g_vision.cam_rgb);
    close_camera(g_vision.cam_ir);

    HYYRobotBase::DevicePoweroff();
    fprintf(stderr, "\n[MAIN] Exited normally.\n");
    return 0;
}
#pragma GCC diagnostic pop

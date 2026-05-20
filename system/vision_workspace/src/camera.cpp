/*
 * camera.cpp - V4L2相机驱动和Epoll管理器模块实现
 */

#include "camera.h"

//============================================================
//            检测是否有可用的图形界面
//============================================================

bool check_display_available() {
    const char* display = getenv("DISPLAY");
    if (display == nullptr || strlen(display) == 0) {
        return false;
    }

    // 检查是否在容器中运行
    FILE* fp = fopen("/proc/1/cmdline", "r");
    if (fp) {
        char cmdline[256] = {0};
        if (fread(cmdline, 1, sizeof(cmdline) - 1, fp) > 0) {
            // 检查是否在容器中
            if (strstr(cmdline, "docker") || strstr(cmdline, "containerd") ||
                strstr(cmdline, "kubelet") || strstr(cmdline, "systemd")) {
                fclose(fp);
                printf("[Camera] 检测到容器环境，禁用 GUI\n");
                return false;
            }
        }
        fclose(fp);
    }
    return true;  // DISPLAY 存在，假设可用
}

//============================================================
//            相机扫描函数实现
//============================================================

void scan_cameras(std::vector<std::string>& rgb_devices, std::vector<std::string>& ir_devices) {
    printf("[Camera] 扫描视频设备 0-9...\n");
    for (int i = 0; i < 10; i++) {
        char dev_path[32];
        snprintf(dev_path, sizeof(dev_path), "/dev/video%d", i);//字符串拼接函数，将字符和数字拼接起来

        int fd = open(dev_path, O_RDWR | O_NONBLOCK);//非阻塞，可读可写
        if (fd < 0) continue;

        v4l2_capability cap;//结构体用来存放摄像头的所有信息
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {    //querycap  查询摄像头能力，并存到cap里，如果<0，说明查询失败，关闭文件并跳过他
            close(fd);
            continue;
        }

        std::string card_name = (char*)cap.card;//c++的字符串数组，可以自动延长，card用于存放摄像头的名字，因为card是无符号字符数组，但是print要求普通字符串数组char,所以要强制转换
        printf("[Camera] 发现 %s: %s\n", dev_path, card_name.c_str());

        v4l2_fmtdesc fmtdesc{};  //查询摄像头支持哪些格式
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  //要查询的是视频捕获的格式
        bool has_color_format = false;
        bool has_grey_format = false;

        while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {   //VIDIOC_ENUM_FMT枚举视频格式，然后装入这个结构体
            // 灰度/IR 格式 (Y8, Y12, Y16, GREY)
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_GREY ||
                fmtdesc.pixelformat == v4l2_fourcc('Y','8',' ',' ') ||//v4l2_fourcc 将四个字符转换成32位数字
                fmtdesc.pixelformat == v4l2_fourcc('Y','1','2',' ') ||
                fmtdesc.pixelformat == v4l2_fourcc('Y','1','6',' ')) {
                has_grey_format = true;
            }
            // 彩色格式
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV ||
                fmtdesc.pixelformat == v4l2_fourcc('U','Y','V','Y') ||
                fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG ||
                fmtdesc.pixelformat == v4l2_fourcc('R','G','B','3') ||
                fmtdesc.pixelformat == v4l2_fourcc('R','G','B','8')) {
                has_color_format = true;
            }
            fmtdesc.index++;
        }
        close(fd);

        // IR/GREY 优先（RealSense IR 设备可能有 UYVY+GREY，应识别为 IR）
        if (has_grey_format) {
            ir_devices.push_back(dev_path);
            printf("  -> IR 候选\n");
        } else if (has_color_format) {
            rgb_devices.push_back(dev_path);
            printf("  -> RGB 候选\n");
        }
    }
    printf("[Camera] 扫描完成: RGB=%zu, IR=%zu\n", rgb_devices.size(), ir_devices.size());
}

bool find_rgb_camera(std::string& out_device_path) {
    printf("[Camera] 扫描可用视频设备...\n");

    std::vector<std::string> rgb_devices, ir_devices;//vector相当于动态数组，可以自动伸缩，string相当于里面装字符串
    scan_cameras(rgb_devices, ir_devices);

    if (!rgb_devices.empty()) {
        out_device_path = rgb_devices.back();//back和empty是vector自带的
        printf("[Camera] 选择 RGB 设备: %s\n", out_device_path.c_str());
        return true;
    }

    printf("[Camera] 警告: 未找到 RGB 摄像头，使用默认设备\n");
    out_device_path = "/dev/video6";
    return true;
}

//============================================================
//            相机初始化和清理实现
//============================================================

bool init_camera(const char* dev, CameraConfig& cam) {   //摄像头配置的结构体
    cam = CameraConfig{};
    cam.device_path = dev;
    cam.fd = open(dev, O_RDWR | O_NONBLOCK);
    if (cam.fd < 0) { perror("open"); return false; }

    v4l2_capability cap;
    if (ioctl(cam.fd, VIDIOC_QUERYCAP, &cap) < 0) { close(cam.fd); return false; }
    cam.device_name = (char*)cap.card;
    printf("[Camera] %s: %s\n", dev, cam.device_name.c_str());

    // 列出所有支持的格式
    v4l2_fmtdesc fmtdesc{};   //vfmtdesc查询摄像头支持的视频格式，存储在fmtdesc列表里，{}吧结构体里的所有成员清零
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;   //拿摄像头拍出来的画面
    printf("[Camera] 支持的格式: ");
    std::vector<__u32> supported_formats;  //<_u32>表示里面只能存32位的数据
    while (ioctl(cam.fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {  //枚举所有的格式，并将格式信息填到fmtdesc里，==0表示成功
        supported_formats.push_back(fmtdesc.pixelformat);//当前格式存放到列表里
        char fourcc[5] = {0};
        fourcc[0] = fmtdesc.pixelformat & 0xFF;  //pixelformat用于存储帧信息
        fourcc[1] = (fmtdesc.pixelformat >> 8) & 0xFF;
        fourcc[2] = (fmtdesc.pixelformat >> 16) & 0xFF;
        fourcc[3] = (fmtdesc.pixelformat >> 24) & 0xFF;//pixelformat存储的数字，这个代码，将这个数字转换成字符，存储到fourcc
        printf("%s ", fourcc);
        fmtdesc.index++;
    }
    printf("\n");

    // 尝试设置格式，按优先级选择（YUYV/UYVY 优先用于彩色）
    __u32 try_formats[] = {
        V4L2_PIX_FMT_YUYV,           // YUYV 4:2:2
        v4l2_fourcc('U','Y','V','Y'), // UYVY 4:2:2 (RealSense RGB 常用)
        v4l2_fourcc('R','G','B','3'), // RGB24
        V4L2_PIX_FMT_MJPEG,          // MJPEG
        0
    };
    int try_widths[] = {640, 848, 1280};
    int try_heights[] = {480, 480, 720};

    bool format_set = false;
    for (size_t fi = 0; fi < sizeof(try_formats)/sizeof(try_formats[0]) && !format_set; fi++) {  //size_t与u32区别   size_t随系统位数改变64、32位，u8,u16分别代表无符号的8.16位
        if (try_formats[fi] == 0) break;

        bool format_supported = false;
        for (auto f : supported_formats) {   //遍历列表supported_formats，将里面的每个取出并放到f
            if (f == try_formats[fi]) { format_supported = true; break; }
        }
        if (!format_supported) continue;

        for (size_t ri = 0; ri < sizeof(try_widths)/sizeof(try_widths[0]); ri++) { //ri < sizeof(try_widths)/sizeof(try_widths[0])计算数组里有多少个元素
            v4l2_format fmt{}; 
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//我要设置的是【摄像头采集的视频】
            fmt.fmt.pix.width  = try_widths[ri];
            fmt.fmt.pix.height = try_heights[ri];
            fmt.fmt.pix.pixelformat = try_formats[fi];//尝试各种画面和帧格式搭配
            fmt.fmt.pix.field = V4L2_FIELD_NONE;

            if (ioctl(cam.fd, VIDIOC_S_FMT, &fmt) == 0) {  //将fmt的参数设置到摄像头中
                if (ioctl(cam.fd, VIDIOC_G_FMT, &fmt) == 0) {   //读取当前的格式并存入fmt中，并将fmt的值赋给cam,相当于将相机的参数存储到cam中
                    cam.width = fmt.fmt.pix.width;
                    cam.height = fmt.fmt.pix.height;
                    cam.pixel_format = fmt.fmt.pix.pixelformat;
                    cam.is_yuyv = (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV);
                    cam.is_uyvy = (fmt.fmt.pix.pixelformat == v4l2_fourcc('U','Y','V','Y'));
                    cam.is_rgb = (fmt.fmt.pix.pixelformat == v4l2_fourcc('R','G','B','3'));
                    cam.is_mjpeg = (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG);
                    format_set = true;

                    char fourcc[5] = {0};
                    fourcc[0] = cam.pixel_format & 0xFF;
                    fourcc[1] = (cam.pixel_format >> 8) & 0xFF;
                    fourcc[2] = (cam.pixel_format >> 16) & 0xFF;
                    fourcc[3] = (cam.pixel_format >> 24) & 0xFF;
                    printf("[Camera] 使用格式: %dx%d %s\n", cam.width, cam.height, fourcc);
                    break;
                }
            }
        }
    }

    if (!format_set) {
        // 如果前面的设置失败了，就回退到默认设置，将默认参数设置给fmt,并将fmt参数设置到cam中，然后从cam读取参数并覆盖到fmt,cam.fd是给真实相机，cam.width是自留的记录本
        v4l2_format fmt{};  //设置摄像头使用的
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width  = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(cam.fd, VIDIOC_S_FMT, &fmt) < 0) { close(cam.fd); return false; }
        if (ioctl(cam.fd, VIDIOC_G_FMT, &fmt) < 0) { close(cam.fd); return false; }
        cam.width = fmt.fmt.pix.width;
        cam.height = fmt.fmt.pix.height;
        cam.pixel_format = fmt.fmt.pix.pixelformat;
        cam.is_yuyv = true;
        printf("[Camera] 使用默认格式: %dx%d YUYV\n", cam.width, cam.height);
    }

    // 设置帧率
    v4l2_streamparm stream_param{};
    stream_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam.fd, VIDIOC_G_PARM, &stream_param) == 0) {  //从摄像头读取流参数
        if (stream_param.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) { //判断是否支持手动设置帧率，按位与，例如 capability是101100，V4L2_CAP_TIMEPERFRAME是100000，与后 是100000，不等于0就是不支持
            stream_param.parm.capture.timeperframe.numerator = 1;
            stream_param.parm.capture.timeperframe.denominator = 30; //设置30帧
            ioctl(cam.fd, VIDIOC_S_PARM, &stream_param);
        }
    }

    v4l2_requestbuffers req{};
    req.count = 4;//申请四个缓冲区
    req.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;//内存模式为mmap
    if (ioctl(cam.fd, VIDIOC_REQBUFS, &req) < 0) { close(cam.fd); return false; }//提交申请

    for (int i = 0; i < 4; i++) { //创建缓冲区结构体
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(cam.fd, VIDIOC_QUERYBUF, &buf) < 0) { close(cam.fd); return false; }
        cam.bufs[i]   = mmap(NULL, 
            buf.length, //一帧图像的大小
            PROT_READ|PROT_WRITE, //映射后的内存权限，可读，可写
            MAP_SHARED, //共享映射，程序看到的内存和摄像头内核看到的内存是同一块，就是共享内存。
            cam.fd, 
            buf.m.offset);  //映射第i号缓冲区。
        cam.buf_len[i] = buf.length;//记录每个缓冲区的大小，后续读取录像使用
        if (cam.bufs[i] == MAP_FAILED) { close(cam.fd); return false; }
        if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) { close(cam.fd); return false; }//这个缓冲区是空的，请填满画面
    }

    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam.fd, VIDIOC_STREAMON, &t) < 0) { close(cam.fd); return false; }
    printf("[Camera] 初始化成功\n");
    return true;
}

void cleanup_camera(CameraConfig& cam) {
    if (cam.fd < 0) return;
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam.fd, VIDIOC_STREAMOFF, &t);   //停止采集

    for (int i = 0; i < 4; i++) {
        if (cam.bufs[i] && cam.bufs[i] != MAP_FAILED) {
            munmap(cam.bufs[i], cam.buf_len[i]); //取消内存映射，第i个映射内存的起始地址，这段内存的长度
        }
    }
    close(cam.fd);
    cam.fd = -1;//给fd打上已关闭的操作
}

bool grab_frame(CameraConfig& cam, cv::Mat& bgr) {  //摄像头采集图像并转换成Opencv的格式
    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    int ret = ioctl(cam.fd, VIDIOC_DQBUF, &buf);  //取出一帧数据
    if (ret < 0) {
        if (errno == EAGAIN) return false;//说明摄像头还没准备好，返回错误
        return false;
    }
    if (buf.index >= 4) { ioctl(cam.fd, VIDIOC_QBUF, &buf); return false; }  //因为帧只有3帧，0123，所以当编号≥4，说明异常，返回错误

    if (cam.is_yuyv) {
        cv::Mat yuyv(cam.height, cam.width, CV_8UC2, cam.bufs[buf.index]); //Mat是一个类，yuyv,定义了图片的长宽，每个图像8位，四个字节存两个像素，cam.bufs[buf.index]一帧图像的裸地址
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);//将格式转为opencv标准的bgr格式
    } else if (cam.is_uyvy) {
        cv::Mat uyvy(cam.height, cam.width, CV_8UC2, cam.bufs[buf.index]);
        cv::cvtColor(uyvy, bgr, cv::COLOR_YUV2BGR_UYVY);
    } else if (cam.is_mjpeg) {
        std::vector<unsigned char> jpeg_data((unsigned char*)cam.bufs[buf.index],
                                              (unsigned char*)cam.bufs[buf.index] + buf.bytesused); //摄像头发过来一张jpg数据，数据有多少字节，把jpg数据存储到数组里
        cv::Mat decoded = cv::imdecode(jpeg_data, cv::IMREAD_COLOR);//解压jpg
        if (decoded.empty()) {
            ioctl(cam.fd, VIDIOC_QBUF, &buf);
            return false;
        }
        decoded.copyTo(bgr);
    } else if (cam.is_rgb) {
        cv::Mat rgb(cam.height, cam.width, CV_8UC3, cam.bufs[buf.index]);
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    } else if (cam.is_grey) {
        cv::Mat grey(cam.height, cam.width, CV_8UC1, cam.bufs[buf.index]);
        cv::cvtColor(grey, bgr, cv::COLOR_GRAY2BGR);
    } else {
        ioctl(cam.fd, VIDIOC_QBUF, &buf);
        return false;
    }

    ioctl(cam.fd, VIDIOC_QBUF, &buf);
    return true;
}

//============================================================
//            V4L2 Epoll管理器实现
//============================================================

V4L2EpollManager::V4L2EpollManager() : epoll_fd(-1), wakeup_fd(-1) {}//把两个文件句柄初始化为-1

V4L2EpollManager::~V4L2EpollManager() {
    cleanup();//对象销毁时自动清理资源，防止内存泄漏，文件忘记关
}

bool V4L2EpollManager::init() {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);//创建epoll,返回句柄
    if (epoll_fd < 0) {
        perror("epoll_create1");
        return false;
    }

    wakeup_fd = eventfd(0, EFD_NONBLOCK);//用来唤醒闹钟
    if (wakeup_fd < 0) {
        perror("eventfd");
        close(epoll_fd);
        epoll_fd = -1;
        return false;
    }

    struct epoll_event ev{};//创建一个epoll事件结构体
    ev.events = EPOLLIN;//监听闹钟有没有响
    ev.data.ptr = nullptr;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wakeup_fd, &ev) < 0) {//把时钟唤醒添加到监听，闹钟用于唤醒睡眠的阻塞程序
        perror("epoll_ctl wakeup_fd");
        close(wakeup_fd);
        close(epoll_fd);
        wakeup_fd = -1;
        epoll_fd = -1;
        return false;
    }

    printf("[Epoll] 初始化成功\n");
    return true;
}

void V4L2EpollManager::cleanup() {
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

bool V4L2EpollManager::add_camera(CameraConfig& cam, bool is_rgb) { //添加一个摄像头到事件循环
    if (epoll_fd < 0) return false;

    int flags = fcntl(cam.fd, F_GETFL, 0);//将摄像头设置为非阻塞模式
    if (!(flags & O_NONBLOCK)) {
        fcntl(cam.fd, F_SETFL, flags | O_NONBLOCK);
    }

    CameraContext* ctx = new CameraContext();//向系统要一块内存，存储cameracontext,创建一个新的相机上下文对象，返回地址，存储到ctx
    ctx->cam = &cam;
    ctx->is_rgb = is_rgb;
    contexts[cam.fd] = ctx; //context[key]=value 将键值进行匹配

    struct epoll_event ev{};//创建一个epoll事件结构体
    ev.events = EPOLLIN | EPOLLET;// EPOLLIN可读事件，相机来新帧就通知我
    ev.data.ptr = ctx;// 把相机数据 ctx 绑在这个事件上

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cam.fd, &ev) < 0) {//将摄像头注册到epoll里
        perror("epoll_ctl ADD");//失败了清理
        delete ctx;
        contexts.erase(cam.fd);
        return false;
    }

    printf("[Epoll] 添加摄像头 %s (fd=%d, %s)\n",
           cam.device_name.c_str(),//摄像头名字
           cam.fd, 
           is_rgb ? "RGB" : "IR");
    return true;
}

bool V4L2EpollManager::start() {
    if (running.exchange(true)) return false;//running.exchange(true)，修改成exchange后的值，并返回原来的参数。

    event_thread = std::thread(&V4L2EpollManager::event_loop, this);//创建线程，开始跑event_loop.本质上是睡眠，然后用线程进行监听2
    return true;
}

void V4L2EpollManager::stop() {
    if (!running.exchange(false)) return;

    if (wakeup_fd >= 0) {
        uint64_t one = 1;
        ssize_t ret = write(wakeup_fd, &one, sizeof(one));//一写入数据，闹钟就触发epoll 立刻醒过来子线程就可以退出循环、结束工作
        (void)ret;  //write收到返回值赋值给ret,并且不用返回。
    }

    if (event_thread.joinable()) {
        event_thread.join();//等待线程结束
    }
}

bool V4L2EpollManager::get_frame(CameraConfig& cam, cv::Mat& frame, int timeout_ms) {//输入哪个相机，等待时间
    auto it = contexts.find(cam.fd);
    if (it == contexts.end()) return false;//找不到直接返回

    CameraContext* ctx = it->second;//second固定位value
    std::unique_lock<std::mutex> lock(ctx->queue_mutex);//unique是智能锁，自动手动都可以，lock_gard是傻瓜锁，智能自动不能手动

    if (timeout_ms > 0) {//需要等待，直接阻塞在这里
        ctx->queue_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {//等到队列有新帧，超时时间到，程序退出 queue_cv是线程的闹钟，lock睡觉时自动解锁，睡醒了上锁，timeout_ms是超时时间，
                                                                                    // [&]() { ... }是一个判断条件，醒来后检查该不该真醒
            return !ctx->frame_queue.empty() || !running;//队列不为空或者程序要停止了，退出
        });
    }

    if (ctx->frame_queue.empty()) return false;//等完了，队列还是空，返回

    frame = ctx->frame_queue.front();//那最前面的一帧，并赋值给cvmat
    ctx->frame_queue.pop_front();,把这一帧删除
    return true;
}

bool V4L2EpollManager::try_get_frame(CameraConfig& cam, cv::Mat& frame) {//获取帧
    return get_frame(cam, frame, 0);
}

size_t V4L2EpollManager::get_queue_size(CameraConfig& cam) {//查询当前相机已经缓存但未取出的帧数量
    auto it = contexts.find(cam.fd);//context实际上是一个相机的哈希表，用于存储相机的编号fd和全部数据,find是用key找value
    if (it == contexts.end()) return 0;

    std::lock_guard<std::mutex> lock(it->second->queue_mutex);
    return it->second->frame_queue.size();
}

void V4L2EpollManager::clear_queue(CameraConfig& cam) {
    auto it = contexts.find(cam.fd);
    if (it == contexts.end()) return;

    std::lock_guard<std::mutex> lock(it->second->queue_mutex);  //it->first → key（fd 编号），it->second → value（相机的所有数据）
    it->second->frame_queue.clear();
}

void V4L2EpollManager::event_loop() {
    printf("[Epoll] 事件循环启动\n");

    struct epoll_event events[EPOLL_MAX_EVENTS];//用于存放就绪事件的数组

    while (running) {
        //阻塞，直到
        //相机来帧了
        //超时时间到
        //被唤醒信号打断
        int nfds = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, EPOLL_TIMEOUT_MS);

        for (int i = 0; i < nfds; i++) {//遍历所有就绪的文件描述符
            if (events[i].data.ptr == nullptr) {
                if (events[i].data.fd == wakeup_fd) {
                    //如果指针为空，且是唤醒fd,读一下,清空信号
                    uint64_t val;
                    ssize_t ret = read(wakeup_fd, &val, sizeof(val));
                    (void)ret;
                }
                continue;
            }

            CameraContext* ctx = (CameraContext*)events[i].data.ptr;//拿到相机上下文
            
            if (events[i].events & EPOLLIN) {//如果是可读事件，就将这一帧放进队列
                process_camera_data(ctx);
            }
        }
    }

    printf("[Epoll] 事件循环结束\n");
}

void V4L2EpollManager::process_camera_data(CameraContext* ctx) {//从摄像头那画面，存到队列里，通知别人来取，，指针用ctx->cam,普通变量用ctx.cam
    CameraConfig* cam = ctx->cam;//将ctx.cam的东西存放到*cam里
    cv::Mat frame;

    while (running) {
        if (!grab_frame(*cam, frame)) {
            break;
        }

        std::lock_guard<std::mutex> lock(ctx->queue_mutex);//std::lock_guard自动加锁解锁，给queue_mutex加锁，mutex是类型

        if (ctx->frame_queue.size() >= FRAME_QUEUE_SIZE) {
            ctx->frame_queue.pop_front();//队列满了就删除最老的一行，把新的一行放进队尾
        }
        ctx->frame_queue.push_back(frame.clone());
        ctx->queue_cv.notify_one();//通知别人，图来了，快来拿
    }
}

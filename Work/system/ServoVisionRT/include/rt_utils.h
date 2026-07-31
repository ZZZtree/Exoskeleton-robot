#ifndef RT_UTILS_H
#define RT_UTILS_H

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <atomic>
#include <cstdio>

namespace rt {

// ---------------------------------------------------------------------------
// Lock memory pages to prevent swapping (MUST call before real-time tasks)
// ---------------------------------------------------------------------------
inline void lock_memory() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "[RT-WARN] mlockall failed: %s\n", strerror(errno));
    }
}

// ---------------------------------------------------------------------------
// Configure a thread as real-time (SCHED_FIFO)
// ---------------------------------------------------------------------------
inline int set_realtime_thread(pthread_t thread, int priority) {
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = priority;

    int err = pthread_setschedparam(thread, SCHED_FIFO, &sp);
    if (err != 0) {
        fprintf(stderr, "[RT-ERROR] pthread_setschedparam(SCHED_FIFO, prio=%d) failed: %s\n",
                priority, strerror(err));
        return -1;
    }

    pthread_getschedparam(thread, &err, &sp);
    fprintf(stderr, "[RT-INFO] Thread %lu set to SCHED_FIFO, priority=%d\n",
            (unsigned long)thread, sp.sched_priority);
    return 0;
}

// ---------------------------------------------------------------------------
// Set thread affinity to specific CPU cores (core 0,1 = shared; 2,3 = isolated)
// ---------------------------------------------------------------------------
inline int set_cpu_affinity(pthread_t thread, const cpu_set_t& cpuset) {
    int err = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (err != 0) {
        fprintf(stderr, "[RT-ERROR] pthread_setaffinity_np failed: %s\n", strerror(err));
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// High-precision busy-wait sleep (for sub-millisecond alignment)
// ---------------------------------------------------------------------------
inline void busy_wait_until(const struct timespec* deadline) {
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, nullptr) == EINTR) {
        // interrupted by signal, retry
    }
}

// ---------------------------------------------------------------------------
// Sleep until deadline using clock_nanosleep (freezes CPU, low power)
// For motor control where the loop does very little computation.
// ---------------------------------------------------------------------------
inline void sleep_until(const struct timespec* deadline) {
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, nullptr) == EINTR) {
        // retry on interrupt
    }
}

// ---------------------------------------------------------------------------
// Compute next deadline: current_time + period_ns
// ---------------------------------------------------------------------------
inline void add_timespec_ns(struct timespec* ts, long long ns) {
    ts->tv_sec += ns / 1000000000LL;
    ts->tv_nsec += ns % 1000000000LL;
    if (ts->tv_nsec >= 1000000000LL) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000LL;
    }
}

// ---------------------------------------------------------------------------
// Get current time as timespec
// ---------------------------------------------------------------------------
inline void get_time(struct timespec* ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

// ---------------------------------------------------------------------------
// Periodic timer: call update() every period_ns, returns elapsed cycles
// Uses absolute clock_nanosleep so period jitter is minimal.
// ---------------------------------------------------------------------------
class PeriodicTask {
public:
    PeriodicTask(long long period_ns, const char* name = "PeriodicTask")
        : period_ns_(period_ns), name_(name), expected_count_(0) {}

    // Call once before the loop
    int start() {
        clock_gettime(CLOCK_MONOTONIC, &next_period_);
        expected_count_ = 0;
        return 0;
    }

    // Wait for next period; returns how many periods were expected (usually 1,
    // but may be >1 if previous cycle ran long)
    int wait() {
        sleep_until(&next_period_);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        // Detect overrun
        long long missed = (now.tv_sec - next_period_.tv_sec) * 1000000000LL
                        + (now.tv_nsec - next_period_.tv_nsec);

        if (missed > period_ns_) {
            fprintf(stderr, "[RT-WARN] %s: missed %lld ns (%.1f periods)\n",
                    name_, missed, (double)missed / period_ns_);
        }

        expected_count_ += (missed / period_ns_) + 1;
        add_timespec_ns(&next_period_, period_ns_);

        return static_cast<int>(expected_count_ - 1);
    }

    long long period_ns() const { return period_ns_; }
    long long missed() const { return expected_count_ - 1; }

private:
    long long period_ns_;
    const char* name_;
    long long expected_count_;  // cumulative expected periods
    struct timespec next_period_;
};

// ---------------------------------------------------------------------------
// Scoped thread starter with RT attributes
// ---------------------------------------------------------------------------
struct ThreadAttr {
    int priority   = 50;
    int cpu_core   = -1;   // -1 = no affinity restriction
    bool realtime  = true; // SCHED_FIFO if true, SCHED_OTHER if false
    bool lock_mem  = true; // mlockall before starting

    ThreadAttr& with_priority(int p)    { priority = p; return *this; }
    ThreadAttr& on_cpu(int c)           { cpu_core = c;  return *this; }
    ThreadAttr& not_realtime()          { realtime = false; return *this; }
    ThreadAttr& no_lock()               { lock_mem = false; return *this; }
};

// ---------------------------------------------------------------------------
// Launch a thread with RT attributes
// ---------------------------------------------------------------------------
template <typename Fn, typename... Args>
pthread_t create_rt_thread(Fn&& fn, Args&&... args) {
    return create_rt_thread_with_attr(ThreadAttr{}, std::forward<Fn>(fn), std::forward<Args>(args)...);
}

template <typename Fn, typename... Args>
pthread_t create_rt_thread_with_attr(const ThreadAttr& attr, Fn&& fn, Args&&... args) {
    pthread_t tid;
    pthread_attr_t attr_obj;

    pthread_attr_init(&attr_obj);
    pthread_attr_setinheritsched(&attr_obj, PTHREAD_EXPLICIT_SCHED);

    if (attr.realtime) {
        pthread_attr_setschedpolicy(&attr_obj, SCHED_FIFO);
        struct sched_param sp;
        sp.sched_priority = attr.priority;
        pthread_attr_setschedparam(&attr_obj, &sp);
    } else {
        pthread_attr_setschedpolicy(&attr_obj, SCHED_OTHER);
    }

    if (attr.cpu_core >= 0) {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(attr.cpu_core, &cpus);
        pthread_attr_setaffinity_np(&attr_obj, sizeof(cpu_set_t), &cpus);
    }

    if (attr.lock_mem) {
        lock_memory();
    }

    pthread_attr_setguardsize(&attr_obj, 0); // no guard page, save VM
    pthread_create(&tid, &attr_obj, std::forward<Fn>(fn), std::forward<Args>(args)...);
    pthread_attr_destroy(&attr_obj);

    return tid;
}

} // namespace rt

#endif // RT_UTILS_H

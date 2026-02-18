#ifndef ORDER_RATE_LIMITER_H
#define ORDER_RATE_LIMITER_H

#include <cstring>
#include <cstdint>
#include <chrono>
#include <thread>
// Ultra-low latency rate limiter with RDTSC timing
class OrderRateLimiter
{
private:
    // Configurable parameters
    size_t queue_capacity;
    double window_seconds;
    std::string module = "RATE_LIMITER";

    // RDTSC cycles for configured window
    uint64_t cycles_per_second;
    uint64_t window_cycles;

    uint64_t *order_timestamps; // Dynamic allocation
    size_t write_index;
    size_t oldest_index; // Track oldest entry for O(1) expiry check

    // RDTSC for ultra-low latency timing (~10ns vs ~100ns for chrono)
    inline uint64_t getRdtsc() const
    {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }

public:
    // Constructor with configurable capacity and time window
    // capacity: Max orders in time window (default: 90)
    // window_sec: Time window in seconds (default: 1.0)
    OrderRateLimiter(size_t capacity = 90, double window_sec = 1.0)
        : queue_capacity(capacity),
          window_seconds(window_sec),
          write_index(0),
          oldest_index(0)
    {

        order_timestamps = new uint64_t[queue_capacity];
        memset(order_timestamps, 0, sizeof(uint64_t) * queue_capacity);

        calibrateTSC();

        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf),
                 "[INFO] OrderRateLimiter initialized: %zu orders per %.2f seconds",
                 queue_capacity, window_seconds);
        LOG_FILE(module, log_buf);
    }

    ~OrderRateLimiter()
    {
        delete[] order_timestamps;
    }

    // Allow reconfiguration at runtime
    void reconfigure(size_t new_capacity, double new_window_sec)
    {
        delete[] order_timestamps;

        queue_capacity = new_capacity;
        window_seconds = new_window_sec;
        window_cycles = static_cast<uint64_t>(cycles_per_second * window_seconds);

        order_timestamps = new uint64_t[queue_capacity];
        memset(order_timestamps, 0, sizeof(uint64_t) * queue_capacity);

        write_index = 0;
        oldest_index = 0;

        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf),
                 "[INFO] OrderRateLimiter reconfigured: %zu orders per %.2f seconds",
                 queue_capacity, window_seconds);
        LOG_FILE(module, log_buf);
    }

    // Calibrate TSC frequency on startup
    void calibrateTSC()
    {
        auto start_chrono = std::chrono::steady_clock::now();
        uint64_t start_tsc = getRdtscp();

        // Sleep for 100ms to calibrate
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t end_tsc = getRdtscp();
        auto end_chrono = std::chrono::steady_clock::now();

        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              end_chrono - start_chrono)
                              .count();

        uint64_t elapsed_cycles = end_tsc - start_tsc;

        // Extrapolate to 1 second
        cycles_per_second = (elapsed_cycles * 1'000'000'000ULL) / elapsed_ns;

        // Calculate cycles for configured window
        window_cycles = static_cast<uint64_t>(cycles_per_second * window_seconds);

        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf),
                 "[INFO] TSC calibrated: %lu cycles/sec (%.2f GHz), window: %lu cycles",
                 cycles_per_second, cycles_per_second / 1e9, window_cycles);
        LOG_FILE(module, log_buf);
    }

    // Ultra-fast check: O(1) operation using RDTSC cycles
    // Returns true if can send, false if blocked
    inline bool canSendAndRecord(uint64_t current_cycles)
    {
        // Check if oldest entry is expired (> 1 second worth of cycles)
        if (order_timestamps[oldest_index] != 0 &&
            (current_cycles - order_timestamps[oldest_index]) > cycles_per_second)
        {
            // Oldest expired - we have space, reuse that slot
            order_timestamps[oldest_index] = current_cycles;
            oldest_index = (oldest_index + 1) % queue_capacity;
            return true;
        }

        // Check if we haven't filled the buffer yet
        if (order_timestamps[write_index] == 0)
        {
            // Empty slot available
            order_timestamps[write_index] = current_cycles;
            write_index = (write_index + 1) % queue_capacity;
            return true;
        }

        // Buffer full and oldest not expired - BLOCK
        return false;
    }

    inline uint64_t getRdtscp() const
    {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
        return ((uint64_t)hi << 32) | lo;
    }
};

#endif // ORDER_RATE_LIMITER_H
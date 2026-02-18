#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// --- Ring Buffer ---
template <typename T, size_t N>
class RingBufferSHM
{
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) T buffer_[N];

public:
    bool push(const T &val)
    {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        buffer_[head] = val;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T &val)
    {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        val = buffer_[tail];
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return true;
    }
};

// --- Shared Memory ---
template <typename T>
class SharedMemory
{
    T *ptr_ = nullptr;
    size_t count_;
    int fd_;
    std::string name_;

public:
    SharedMemory(const std::string &name, size_t count, bool create = false)
        : count_(count), name_(name)
    {
        int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
        fd_ = shm_open(name.c_str(), flags, 0666);
        if (fd_ < 0)
        {
            perror("shm_open");
            exit(1);
        }

        size_t size = sizeof(T) * count;
        if (create && ftruncate(fd_, size) != 0)
        {
            perror("ftruncate");
            exit(1);
        }

        void *addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (addr == MAP_FAILED)
        {
            perror("mmap");
            exit(1);
        }

        ptr_ = reinterpret_cast<T *>(addr);
    }

    ~SharedMemory()
    {
        munmap(ptr_, sizeof(T) * count_);
        close(fd_);
    }

    void unlink()
    {
        shm_unlink(name_.c_str());
    }

    T *data() { return ptr_; }
    const std::string &name() const { return name_; }
};

// --- LogEntry + Types ---
struct LogEntry
{
    uint64_t timestamp_ns;
    char module[32];
    char text[192];
};

using LogBuffer = RingBufferSHM<LogEntry, 4096>;

// --- Logger Class ---
class ShmLogger
{
    std::string shm_name_;
    SharedMemory<LogBuffer> shm_;
    LogBuffer *buffer_;

public:
    ShmLogger(const std::string &shm_name, bool create = false)
        : shm_name_(shm_name), shm_(shm_name, 1, create)
    {
        buffer_ = shm_.data();
    }

    template <typename... Args>
    void log(const char *module, Args &&...args)
    {
        LogEntry entry;
        entry.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        std::snprintf(entry.module, sizeof(entry.module), "%s", module);

        std::ostringstream oss;

        // Use fold expression with comma separator
        ((oss << args << ", "), ...);

        std::string logText = oss.str();

        // Remove trailing comma + space if present
        if (!logText.empty())
        {
            logText.erase(logText.size() - 2);
        }

        std::snprintf(entry.text, sizeof(entry.text), "%s", logText.c_str());
        buffer_->push(entry);
    }

    SharedMemory<LogBuffer> *shm() { return &shm_; }
};

// shm_logger.cpp
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <csignal>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <cstring>
#include <ctime>
#include <iomanip>
template <typename T, size_t N>
class RingBuffer
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

template <typename T>
class SharedMemory
{
    T *ptr_ = nullptr;
    size_t count_;
    int fd_;
    std::string name_;

public:
    SharedMemory(const char *name, size_t count, bool create = false)
        : count_(count), name_(name)
    {
        int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
        fd_ = shm_open(name, flags, 0666);
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
};

struct LogEntry
{
    uint64_t timestamp_ns;
    char module[32];
    char text[192];
};

using LogBuffer = RingBuffer<LogEntry, 4096>;

SharedMemory<LogBuffer> *shm_ptr = nullptr;
std::string shm_name;
std::string log_file_name;

void cleanup(int)
{
    if (shm_ptr)
    {
        shm_ptr->unlink();
        std::cout << "Shared memory unlinked.\n";
    }
    exit(0);
}

std::string get_timestamp_ns()
{
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

template <typename... Args>
void LOG_FAST(const char *module, Args &&...args)
{
    SharedMemory<LogBuffer> shm("/log_ring", 1, false);
    LogBuffer *buffer = shm.data();

    LogEntry entry;
    entry.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    // entry.timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    std::snprintf(entry.module, sizeof(entry.module), "%s", module);

    std::ostringstream oss;
    (oss << ... << args); // fold expression for variadic args
    std::snprintf(entry.text, sizeof(entry.text), "%s", oss.str().c_str());

    buffer->push(entry);
}

std::string format_timestamp(uint64_t timestamp_ns)
{
    using namespace std::chrono;

    // Convert nanoseconds to system_clock time_point
    auto tp = system_clock::time_point(nanoseconds(timestamp_ns));

    std::time_t time = system_clock::to_time_t(tp);
    auto fractional = duration_cast<microseconds>(tp.time_since_epoch()).count() % 1000000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%F %T") << "." << std::setw(6) << std::setfill('0') << fractional;
    return oss.str();
}

void run_writer()
{
    SharedMemory<LogBuffer> shm("/log_ring", 1, true);
    // LogBuffer* buffer = shm.data();

    for (int i = 0; i < 100; ++i)
    {
        LOG_FAST("STRATEGY", "Price=", 100 + i, ", Qty=", 10 * i);
        // std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void run_reader(std::string log_name)
{

    SharedMemory<LogBuffer> shm("/log_ring", 1, false);
    shm_ptr = &shm;
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    LogBuffer *buffer = shm.data();
    std::ofstream file(log_name, std::ios::app);

    if (!file.is_open())
    {
        std::cerr << "Failed to open output log file\n";
        return;
    }

    while (true)
    {
        LogEntry entry;
        if (buffer->pop(entry))
        {
            file << "[" << format_timestamp(entry.timestamp_ns) << "] "
                 << "[" << entry.module << "] "
                 << entry.text << "\n";
            file.flush();
            std::cout << "[" << format_timestamp(entry.timestamp_ns) << "] "
                      << "[" << entry.module << "] "
                      << entry.text << "\n";
        }
        else
        {
            // std::cout<<"Data not available waiting for data pausing for 1 milisecond\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

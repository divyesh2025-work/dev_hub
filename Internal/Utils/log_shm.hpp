#ifndef SHM_LOGGER_HPP
#define SHM_LOGGER_HPP

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <type_traits>
#include <new>
#include "../Include/strategy/Portfolio.h"
// Lock-free SPSC ring buffer in shared memory
template <typename T, size_t N>
class ShmQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert((N & (N - 1)) == 0, "N must be power of 2");

    struct Header
    {
        alignas(64) std::atomic<uint64_t> write_idx;
        alignas(64) std::atomic<uint64_t> read_idx;
        alignas(64) char pad[64];
    };

    Header *hdr;
    T *buf;
    int shm_fd;
    void *shm_ptr;
    size_t shm_size;

public:
    static constexpr size_t calc_shm_size()
    {
        return sizeof(Header) + sizeof(T) * N;
    }

    // Producer: create and initialize
    bool create(const char *name)
    {
        shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1)
            return false;

        shm_size = calc_shm_size();
        if (ftruncate(shm_fd, shm_size) == -1)
        {
            close(shm_fd);
            return false;
        }

        shm_ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, shm_fd, 0);
        if (shm_ptr == MAP_FAILED)
        {
            close(shm_fd);
            return false;
        }

        hdr = new (shm_ptr) Header();
        buf = reinterpret_cast<T *>(static_cast<char *>(shm_ptr) + sizeof(Header));

        hdr->write_idx.store(0, std::memory_order_relaxed);
        hdr->read_idx.store(0, std::memory_order_relaxed);

        return true;
    }

    // Consumer: attach to existing
    bool attach(const char *name)
    {
        shm_fd = shm_open(name, O_RDWR, 0666);
        if (shm_fd == -1)
            return false;

        shm_size = calc_shm_size();
        shm_ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, shm_fd, 0);
        if (shm_ptr == MAP_FAILED)
        {
            close(shm_fd);
            return false;
        }

        hdr = static_cast<Header *>(shm_ptr);
        buf = reinterpret_cast<T *>(static_cast<char *>(shm_ptr) + sizeof(Header));

        return true;
    }

    // Ultra-fast push (producer only)
    __attribute__((always_inline)) inline bool push(const T &item)
    {
        uint64_t w = hdr->write_idx.load(std::memory_order_relaxed);
        uint64_t r = hdr->read_idx.load(std::memory_order_acquire);

        if (w - r >= N)
            return false; // Full

        buf[w & (N - 1)] = item;
        hdr->write_idx.store(w + 1, std::memory_order_release);
        return true;
    }

    // Pop (consumer only)
    __attribute__((always_inline)) inline bool pop(T &item)
    {
        uint64_t r = hdr->read_idx.load(std::memory_order_relaxed);
        uint64_t w = hdr->write_idx.load(std::memory_order_acquire);

        if (r == w)
            return false; // Empty

        item = buf[r & (N - 1)];
        hdr->read_idx.store(r + 1, std::memory_order_release);
        return true;
    }

    void cleanup(const char *name)
    {
        if (shm_ptr)
            munmap(shm_ptr, shm_size);
        if (shm_fd != -1)
            close(shm_fd);
        shm_unlink(name);
    }

    ~ShmQueue()
    {
        if (shm_ptr)
            munmap(shm_ptr, shm_size);
        if (shm_fd != -1)
            close(shm_fd);
    }
};

// Example structures
struct TradeLog
{
    uint64_t timestamp_ns;
    uint64_t order_id;
    double price;
    uint32_t quantity;
    char symbol[16];
    uint8_t side; // 0=buy, 1=sell
};

// StrategyState
enum class StrategyState : uint8_t
{
    NewOrder,
    CancelOrder,
    ModifyOrder

};

struct StrategyDataLog
{
    StrategyState msg_type;
    uint16_t pf_id;
    uint32_t oms_order_id;
    uint32_t token;
    uint32_t price;
    uint32_t qty;
    OrderSide side;
    int current_spread;
    int given_spread;
    int diff;
    StrategyMarketSnapshot market_snapshot;
};

// ============================================================================
// ULTRA-FAST GLOBAL LOGGER (Auto-Init at Startup)
// ============================================================================

namespace UltraLog
{
    namespace detail
    {
        template <typename LogT, size_t QueueSize>
        struct Producer
        {
            ShmQueue<LogT, QueueSize> queue;

            Producer(const char *shm_name)
            {
                queue.create(shm_name);
            }

            __attribute__((always_inline)) inline void log(const LogT &entry)
            {
                queue.push(entry);
            }
        };

        // Auto-init at program startup
        inline Producer<TradeLog, 65536> trade_logger("/trade_shm_test");
        inline Producer<StrategyDataLog, 65536> strategy_logger("/strategy_shm_test");
    }

    // Hot path - zero overhead
    __attribute__((always_inline)) inline void trade(const TradeLog &log)
    {
        detail::trade_logger.log(log);
    }

    __attribute__((always_inline)) inline void strategy(const StrategyDataLog &log)
    {
        detail::strategy_logger.log(log);
    }
}

// ============================================================================
// CONSUMER
// ============================================================================

template <typename LogT, size_t QueueSize = 65536>
class FileLogger
{
    ShmQueue<LogT, QueueSize> queue;
    int fd;

public:
    FileLogger() : fd(-1) {}

    bool init(const char *shm_name, const char *log_file)
    {
        if (!queue.attach(shm_name))
            return false;

        fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        return fd != -1;
    }

    void run(size_t batch_size = 512)
    {
        LogT buffer[batch_size];

        while (true)
        {
            size_t count = 0;

            for (size_t i = 0; i < batch_size; ++i)
            {
                if (!queue.pop(buffer[count]))
                    break;
                ++count;
            }

            if (count > 0)
            {
                write(fd, buffer, count * sizeof(LogT));
            }
            else
            {
                usleep(100);
            }
        }
    }

    ~FileLogger()
    {
        if (fd != -1)
            close(fd);
    }
};

// ============================================================================
// UTILITY: RDTSC for timestamps
// ============================================================================

static inline uint64_t rdtsc()
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Producer example
template <typename LogT, size_t QueueSize = 65536>
class FastProducer
{
    ShmQueue<LogT, QueueSize> queue;

public:
    bool init(const char *shm_name)
    {
        return queue.create(shm_name);
    }

    // Hot path - inline everything
    __attribute__((always_inline)) inline void log(const LogT &entry)
    {
        // No error checking in hot path for max speed
        // Queue will drop if full
        queue.push(entry);
    }

    void cleanup(const char *shm_name)
    {
        queue.cleanup(shm_name);
    }
};

#endif // SHM_LOGGER_HPP

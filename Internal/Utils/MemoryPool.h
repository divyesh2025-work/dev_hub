// MemoryPool.h
#pragma once

#include <array>
#include <atomic>

template <typename T, size_t Size>
class MemoryPool
{
private:
    alignas(64) std::array<T, Size> pool;
    alignas(64) std::array<std::atomic<bool>, Size> used;
    std::atomic<size_t> next_index{0};

public:
    MemoryPool()
    {
        for (auto &u : used)
        {
            u.store(false, std::memory_order_relaxed);
        }
    }

    T *acquire()
    {
        size_t start = next_index.load(std::memory_order_relaxed);
        for (size_t i = 0; i < Size; ++i)
        {
            size_t idx = (start + i) % Size;
            bool expected = false;
            if (used[idx].compare_exchange_weak(expected, true, std::memory_order_acq_rel))
            {
                next_index.store((idx + 1) % Size, std::memory_order_relaxed);
                return &pool[idx];
            }
        }
        return nullptr;
    }

    void release(T *ptr)
    {
        if (ptr >= &pool[0] && ptr < &pool[Size])
        {
            size_t idx = ptr - &pool[0];
            used[idx].store(false, std::memory_order_release);
        }
    }
};
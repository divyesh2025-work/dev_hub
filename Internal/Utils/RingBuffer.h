// RingBuffer.h
#pragma once

#include <array>
#include <atomic>

template <typename T, size_t Size>
class RingBuffer
{
private:
    alignas(64) std::array<T, Size> buffer;
    alignas(64) std::atomic<size_t> write_pos{0};
    alignas(64) std::atomic<size_t> read_pos{0};

public:
    size_t size() const
    {
        size_t w = write_pos.load(std::memory_order_acquire);
        size_t r = read_pos.load(std::memory_order_acquire);

        return (w >= r) ? (w - r) : (Size - (r - w));
    }

    bool push(const T &item)
    {
        size_t current_write = write_pos.load(std::memory_order_relaxed);
        size_t next_write = (current_write + 1) % Size;

        if (next_write == read_pos.load(std::memory_order_acquire))
        {
            return false;
        }

        buffer[current_write] = item;
        write_pos.store(next_write, std::memory_order_release);
        return true;
    }

    bool pop(T &item)
    {
        size_t current_read = read_pos.load(std::memory_order_relaxed);
        if (current_read == write_pos.load(std::memory_order_acquire))
        {
            return false;
        }

        item = buffer[current_read];
        read_pos.store((current_read + 1) % Size, std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return read_pos.load(std::memory_order_acquire) ==
               write_pos.load(std::memory_order_acquire);
    }
};
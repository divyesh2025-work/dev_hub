// tcp_ring_buffer.hpp
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

template<typename MessageType, size_t BufferSize = 65536>
class alignas(64) TCPReceiveBuffer {
    static_assert((BufferSize & (BufferSize - 1)) == 0, "BufferSize must be power of 2");
    static_assert(BufferSize >= sizeof(MessageType) * 4, "Buffer too small for message type");
    
private:
    static constexpr size_t SIZE_MASK = BufferSize - 1;
    
    alignas(64) std::array<uint8_t, BufferSize> data;
    size_t read_pos = 0;
    size_t write_pos = 0;
    
public:
    // How many bytes are available to read
    [[nodiscard]] inline size_t available() const noexcept {
        return write_pos - read_pos;
    }
    
    // How much space for receiving more data
    [[nodiscard]] inline size_t space() const noexcept {
        return BufferSize - (write_pos - read_pos);
    }
    
    // Get pointer to write new data (for recv)
    [[nodiscard]] inline uint8_t* write_ptr() noexcept {
        return &data[write_pos & SIZE_MASK];
    }
    
    // Get pointer to read next message
    [[nodiscard]] inline const uint8_t* read_ptr() const noexcept {
        return &data[read_pos & SIZE_MASK];
    }
    
    // How many contiguous bytes can we recv() without wrapping?
    [[nodiscard]] inline size_t contiguous_write_space() const noexcept {
        size_t write_idx = write_pos & SIZE_MASK;
        size_t available_space = space();
        size_t to_end = BufferSize - write_idx;
        return std::min(available_space, to_end);
    }
    
    // How many contiguous bytes available for reading?
    [[nodiscard]] inline size_t contiguous_read_bytes() const noexcept {
        size_t read_idx = read_pos & SIZE_MASK;
        size_t avail = available();
        size_t to_end = BufferSize - read_idx;
        return std::min(avail, to_end);
    }
    
    // Advance write position after recv()
    inline void commit_write(size_t bytes) noexcept {
        write_pos += bytes;
    }
    
    // Advance read position after processing message
    inline void consume(size_t bytes) noexcept {
        read_pos += bytes;
    }
    
    // Peek at message without consuming (for zero-copy access)
    [[nodiscard]] inline const MessageType* peek() const noexcept {
        if (available() < sizeof(MessageType)) [[unlikely]]
            return nullptr;
        
        size_t read_idx = read_pos & SIZE_MASK;
        
        // Check if message wraps around buffer end
        if (read_idx + sizeof(MessageType) <= BufferSize) [[likely]] {
            return reinterpret_cast<const MessageType*>(&data[read_idx]);
        }
        
        return nullptr; // Message spans wrap point - use read_wrapped()
    }
    
    // For messages that span the wrap point (rare case)
    [[nodiscard]] inline bool read_wrapped(MessageType& out) noexcept {
        if (available() < sizeof(MessageType)) [[unlikely]]
            return false;
        
        size_t read_idx = read_pos & SIZE_MASK;
        size_t first_part = BufferSize - read_idx;
        
        if (first_part >= sizeof(MessageType)) [[likely]] {
            // No wrap, direct copy
            std::memcpy(&out, &data[read_idx], sizeof(MessageType));
        } else {
            // Wrapped - copy in two parts
            std::memcpy(&out, &data[read_idx], first_part);
            std::memcpy(reinterpret_cast<uint8_t*>(&out) + first_part,
                       &data[0], sizeof(MessageType) - first_part);
        }
        return true;
    }
    
    // Reset buffer (use with caution - only when reconnecting)
    inline void reset() noexcept {
        read_pos = 0;
        write_pos = 0;
    }
    
    // Get buffer utilization percentage (for monitoring)
    [[nodiscard]] inline float utilization() const noexcept {
        return (100.0f * available()) / BufferSize;
    }
};
#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <x86intrin.h>
#include <unordered_map>

class LatencyMonitor
{
public:
    explicit LatencyMonitor(const char *filepath = "latency.raw") noexcept
        : file_path_(filepath), idx_(0) {}

    inline unsigned long long mark_jx(uint32_t seq) noexcept
    {
        // Capture current time in nanoseconds
        auto st = __rdtsc();
        start_map[seq] = st;

        return st;
    }

    inline void end_send(unsigned long long st) noexcept
    {
        auto end = __rdtsc();
        double latency_ns = ((end - st) / (4.0 * 1e9)) * 1e9;
        std::ofstream out("../latency_SEND.log", std::ios::app);
        if (!out)
        {
            std::cerr << "Failed to open latency.log\n";
            return;
        }
        out << latency_ns << std::endl;
        // //std::cout<<"latency_ns"<<std::endl;
        out.close();
    }

    inline void end_recv(unsigned long long st, unsigned long long end) noexcept
    {

        double latency_ns = ((end - st) / (4.0 * 1e9)) * 1e9;
        std::ofstream out("../latency_RECv.log", std::ios::app);
        if (!out)
        {
            std::cerr << "Failed to open latency.log\n";
            return;
        }
        out << latency_ns << std::endl;
        out.close();
    }

    inline void mark_end_RTT(unsigned long long end, uint32_t st_id) noexcept
    {
        if (start_map.find(st_id) != start_map.end())
        {
            double latency_ns = ((end - start_map[st_id]) / (4.0 * 1e9)) * 1e9;

            std::ofstream out("../latency_RTT.log", std::ios::app);
            if (!out)
            {
                std::cerr << "Failed to open latency.log\n";
                return;
            }
            out << latency_ns << std::endl;
            // //std::cout<<"latency_ns"<<std::endl;
            out.close();
        }
    }

    inline void end_monitor1(unsigned long long st1) noexcept
    {
        auto st = __rdtsc();
        double latency_ns = ((st - st1) / (4.0 * 1e9)) * 1e9;

        std::ofstream out("latency_strategy.log", std::ios::app);
        if (!out)
        {
            std::cerr << "Failed to open latency_tcp.log\n";
            return;
        }
        out << latency_ns << std::endl;
        out.close();
    }
    // Call immediately after you receive the packet / event you want to time
    inline unsigned long long mark_rx(uint64_t seq) noexcept
    { // seq can be any unique identifier (e.g. packet sequence, order id)
        auto st = __rdtsc();

        // push({seq, now_ns(), 0});
        return st;
    }

    inline void start_monitor() noexcept
    { // seq can be any unique identifier (e.g. packet sequence, order id)
        auto st = __rdtsc();
        start_time = st;
        // push({seq, now_ns(), 0});
    }

    inline void end_monitor(unsigned long long st1) noexcept
    { // seq can be any unique identifier (e.g. packet sequence, order id)
        auto st = __rdtsc();
        //    end_time = st;
        double latency_ns = ((st - st1) / (4.0 * 1e9)) * 1e9;
        std::ofstream out("../latency_tcp.log", std::ios::app);
        if (!out)
        {
            std::cerr << "Failed to open latency.log\n";
            return;
        }
        out << latency_ns << std::endl;
        // //std::cout<<"latency_ns"<<std::endl;
        out.close();
        // push({seq, now_ns(), 0});
    }
    // mark_end_RTT

    inline void mark_tx_bid(unsigned long long st1) noexcept
    {
        auto end = __rdtsc();

        // std::cout<<start_time<<" "<<end_time<<":"<< ((end_time - start_time) / (4.0 * 1e9)) * 1e9<<"\n";
        // unsigned long long st4 = st1 > st2 ? st1 : st2;
        // st4 = st4 > st3 ? st4 : st3;
        double latency_ns = ((end - st1) / (4.0 * 1e9)) * 1e9;

        std::ofstream out("latency_strategy.log", std::ios::app);
        if (!out)
        {
            std::cerr << "Failed to open latency.log\n";
            return;
        }
        out << latency_ns << std::endl;
        // //std::cout<<"latency_ns"<<std::endl;
        out.close();
    }

    // Call at the point considered the end of the measured path
    inline void mark_tx(unsigned long long st1, unsigned long long st2, unsigned long long st3) noexcept
    {
        auto end = __rdtsc();

        // std::cout<<start_time<<" "<<end_time<<":"<< ((end_time - start_time) / (4.0 * 1e9)) * 1e9<<"\n";
        unsigned long long st4 = st1 > st2 ? st1 : st2;
        st4 = st4 > st3 ? st4 : st3;
        double latency_ns = ((end - st4) / (4.0 * 1e9)) * 1e9;

        std::ofstream out("latency_strategy.log", std::ios::app);
        if (!out)
        {
            std::cerr << "Failed to open latency.log\n";
            return;
        }
        out << latency_ns << std::endl;
        // //std::cout<<"latency_ns"<<std::endl;
        out.close();
    }

    void flush()
    {
        std::ofstream out(file_path_, std::ios::binary | std::ios::app);
        for (const auto &rec : buf_)
        {
            if (rec.t_tx == 0)
                continue;                           // skip incomplete pairs
            uint64_t latency = rec.t_tx - rec.t_rx; // in nanoseconds
            out.write(reinterpret_cast<const char *>(&latency), sizeof(latency));
        }
        out.close();
        idx_.store(0, std::memory_order_relaxed);
    }

    ~LatencyMonitor() { flush(); }

private:
    struct Record
    {
        uint64_t seq;  // identifier of the logical flow we are timing
        uint64_t t_rx; // timestamp at entry point (ns)
        uint64_t t_tx; // timestamp at exit  point (ns)
    };

    unsigned long long start_time;
    unsigned long long end_time;

    static constexpr size_t BUF_SIZE = 4096; // enough for 400 µs @ 10 Mpps

    // Return current time in nanoseconds since epoch
    static inline uint64_t now_ns() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    inline void push(const Record &r) noexcept
    {
        size_t i = idx_.fetch_add(1, std::memory_order_relaxed);
        buf_[i & (BUF_SIZE - 1)] = r; // power‑of‑two wrap
        if ((i & (BUF_SIZE - 1)) == BUF_SIZE - 1)
            flush(); // write on wrap
    }

    const char *file_path_;
    std::array<Record, BUF_SIZE> buf_{};
    std::unordered_map<uint32_t, unsigned long long> start_map;
    std::atomic<size_t> idx_;
};

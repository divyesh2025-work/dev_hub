#pragma once
#include <cstdint>
#include <cstring>
#include <unordered_set>
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
#pragma pack(push, 1)


struct alignas(64) StoredMarketDataLatency
{
    uint32_t bids[5];
    uint32_t asks[5];
    uint32_t bids_qty[5];
    uint32_t asks_qty[5];

    long start_time;
    uint32_t seqno;
    uint32_t internal_seqno;
    uint32_t last_traded_price;
    uint8_t stream_id;
    char msg_type;
};

struct MarketData
{
    uint32_t token;
    uint32_t bids[5];
    uint32_t asks[5];
    uint32_t bids_qty[5];
    uint32_t asks_qty[5];
    uint32_t seqno;
    char msg_type;
    uint32_t internal_seqno;
    uint8_t stream_id;
    uint32_t last_traded_price;
    long timestamp;    
};

struct StreamData
{
   uint32_t seq_no;
   TimePoint last_time;
   std::unordered_set<uint16_t> portfolio_ids;
};
constexpr int64_t ONE_MICROSECOND = 1000;          // ns
constexpr int64_t ONE_MILLISECOND = 1000000;       // ns
constexpr int64_t ONE_SECOND = 1000000000;         // ns
constexpr int64_t FIVE_SECOND = 5000000000;         // ns
constexpr int64_t THIRTY_SECOND = 30000000000;         // ns
constexpr uint64_t CPU_FREQ = 3000000000ULL; // 3 GHz (set correctly!)
constexpr uint64_t THIRTY_SEC_CYCLES = 30ULL * CPU_FREQ;


#pragma pack(pop)
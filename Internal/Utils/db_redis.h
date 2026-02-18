#pragma once
#include <iostream>
#include <atomic>
#include <thread>
#include <string>
#include <fstream>
#include <hiredis/hiredis.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <immintrin.h>
#include <map>

#include "../Include/Types.h"
#include <unordered_map>
#include "../Library/flat_hash_map.hpp"

// ============================================================================
// ENUMS + STRUCTS
// ============================================================================
enum class ShmMessageType : uint8_t
{
    Portfolio = 1,
    ThreeLegBiddingParams,
    ThreeLegBiddingOrderData,
    ThreeLegIOCParams,
    ThreeLegIOCOrderData,
    BoxBiddingParams,
    BoxBiddingOrderData,
    BoxIOCParams,
    BoxIOCOrderData,
    StrategyLegData,
    Trade,
    LogMessage
};

// ----------------------------------------------------------------------------
// UTILITY: Timestamp + Thread ID helper
// ----------------------------------------------------------------------------
static inline std::string now_str()
{
    auto tp = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%F %T");
    return oss.str();
}

static inline uint64_t tid()
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

struct alignas(64) ShmMessage
{
    ShmMessageType type;
    union
    {
        PortfolioShm portfolio;
        ThreeLegBiddingParams three_leg_bidding_params;
        ThreeLegBiddingOrderData three_leg_bidding_data;
        ConRevParams three_leg_ioc_params;
        ConRevOrderData three_leg_ioc_data;
        BoxIocParams box_ioc_params;
        BoxIocOrderData box_ioc_data;
        BoxBiddingParams box_bidding_params;
        BoxBiddingOrderData box_bidding_data;
        StrategyLegData oms;
        Trade trade;
        LogMessageShm log_shm;
    };
};

// ----------------------------------------------------------------------------
// LOG MESSAGE
// ----------------------------------------------------------------------------
struct alignas(64) LogMsg
{
    uint16_t portfolio_id;
    char text[510];
};

// ============================================================================
// LOCK-FREE QUEUE
// ============================================================================
template <typename T, size_t Capacity>
struct alignas(64) SPSCQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) T buffer[Capacity];

    alignas(64) size_t cached_tail{0};
    alignas(64) size_t cached_head{0};

    bool push(const T &item) noexcept
    {
        size_t h = head.load(std::memory_order_relaxed);
        size_t next = (h + 1) & (Capacity - 1);

        if (next == cached_tail)
        {
            cached_tail = tail.load(std::memory_order_acquire);
            if (next == cached_tail)
                return false;
        }

        buffer[h] = item;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T &item) noexcept
    {
        size_t t = tail.load(std::memory_order_relaxed);

        if (t == cached_head)
        {
            cached_head = head.load(std::memory_order_acquire);
            if (t == cached_head)
                return false;
        }

        item = buffer[t];
        tail.store((t + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
};

// ============================================================================
// SHARED MEMORY CONFIG
// ============================================================================
constexpr size_t DB_QUEUE_SIZE = 1024;
constexpr size_t LOG_QUEUE_SIZE = 4096;

using DBQueue = SPSCQueue<ShmMessage, DB_QUEUE_SIZE>;
using LogQueue = SPSCQueue<LogMsg, LOG_QUEUE_SIZE>;

// ----------------------------------------------------------------------------
// SHM Create/Attach Helper
// ----------------------------------------------------------------------------
template <typename T>
T *create_or_attach_shm(const char *name, bool create)
{
    int fd = shm_open(name, create ? (O_CREAT | O_RDWR) : O_RDWR, 0666);
    if (fd < 0)
    {
        perror("shm_open");
        exit(1);
    }
    if (create)
        ftruncate(fd, sizeof(T));

    void *ptr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    std::cout << now_str() << " [T" << tid() << "] "
              << (create ? "Created" : "Attached")
              << " SHM: " << name << " (size=" << sizeof(T) << " bytes)\n";

    return reinterpret_cast<T *>(ptr);
}

// ============================================================================
// LOGGING SYSTEM
// ============================================================================
inline void log_writer(LogQueue *q)
{
    std::unordered_map<uint16_t, std::ofstream> file_map;
    LogMsg m{};
    size_t counter = 0;

    std::cout << now_str() << " [T" << tid() << "] Log writer started.\n";

    while (true)
    {
        if (!q->pop(m))
        {
            _mm_pause();
            continue;
        }

        uint16_t pf_id = m.portfolio_id;

        if (file_map.find(pf_id) == file_map.end())
        {
            std::string filename = "portfolio_" + std::to_string(pf_id) + ".log";
            std::ofstream &f = file_map[pf_id];
            f.open(filename, std::ios::out | std::ios::app | std::ios::binary);

            if (!f.is_open())
            {
                std::cerr << "[" << now_str() << "] Error: Failed to open log file for portfolio " << pf_id << "\n";
                continue;
            }

            f.rdbuf()->pubsetbuf(nullptr, 0);
            std::cout << "[" << now_str() << "] Opened log file: " << filename << "\n";
        }

        std::ofstream &f = file_map[pf_id];
        f.write(m.text, strlen(m.text));
        f.put('\n');

        if (++counter % 1 == 0)
        {
            f.flush();
        }
    }
}

// ============================================================================
// REDIS HELPER FUNCTIONS
// ============================================================================
inline bool executeRedisCommand(redisContext *ctx, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    redisReply *reply = (redisReply *)redisvCommand(ctx, format, args);
    va_end(args);

    if (!reply)
    {
        std::cerr << "Redis command failed: " << ctx->errstr << std::endl;
        return false;
    }

    bool success = (reply->type != REDIS_REPLY_ERROR);
    if (!success)
    {
        std::cerr << "Redis error: " << reply->str << std::endl;
    }

    freeReplyObject(reply);
    return success;
}

// Redis doesn't need table creation, but we can set up key prefixes
inline bool createAllTables(const char *host, int port)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        if (ctx)
        {
            std::cerr << "Connection error: " << ctx->errstr << std::endl;
            redisFree(ctx);
        }
        else
        {
            std::cerr << "Connection error: can't allocate redis context" << std::endl;
        }
        return false;
    }

    std::cout << "Connected to Redis at " << host << ":" << port << std::endl;
    std::cout << "Redis uses key-value storage, no table creation needed" << std::endl;

    redisFree(ctx);
    return true;
}

// ============================================================================
// REDIS MESSAGE HANDLERS
// ============================================================================
inline void handleShmMessage(const ShmMessage &msg, redisContext *ctx, LogQueue *lq)
{
    switch (msg.type)
    {
    case ShmMessageType::Portfolio:
    {
        std::cout << "Portfolio\n";
        const PortfolioShm &p = msg.portfolio;

        std::cout << "***********************************************************" << p.portfolio_id << "\n";
        std::cout << "Portfolio ID: " << p.portfolio_id << "\n";
        std::cout << "\n=== PORTFOLIO MESSAGE RECEIVED FROM SHM ===\n";
        std::cout << "Portfolio ID: " << p.portfolio_id << "\n";
        std::cout << "Kind: " << (int)p.kind << "\n";
        std::cout << "Leg Count: " << (int)p.leg_count << "\n";
        std::cout << "Active: " << p.is_active << "\n";
        std::cout << "Terminate: " << p.terminate << "\n";
        std::cout << "Iter Over: " << p.is_iter_over << "\n";
        std::cout << "Stop Requested: " << p.stop_requested << "\n";
        std::cout << "Stale Status: " << (int)p.stale_status << "\n";
        std::cout << "Traded Qty: " << p.traded_qty << "\n";
        std::cout << "Achieved Spread: " << p.achieved_spread << "\n";

        for (int i = 0; i < 4; i++)
        {
            std::cout << "Leg[" << i << "]: "
                      << "token=" << p.legs[i].symbol_token
                      << " strike=" << p.legs[i].strike_price
                      << " lotsize=" << p.legs[i].lot_size
                      << " pro=" << p.legs[i].is_pro_account
                      << " type=" << (int)p.legs[i].leg_type
                      << " side=" << (int)p.legs[i].side << "\n";
        }

        // Store as Redis hash
        std::string key = "portfolio:" + std::to_string(p.portfolio_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s "
                                                       "portfolio_id %d kind %d leg_count %d is_active %d terminate %d "
                                                       "is_iter_over %d stop_requested %d stale_status %d traded_qty %u achieved_spread %d "
                                                       "leg1_token %u leg1_strike %u leg1_lotsize %u leg1_pro %d leg1_type %d leg1_side %d "
                                                       "leg2_token %u leg2_strike %u leg2_lotsize %u leg2_pro %d leg2_type %d leg2_side %d "
                                                       "leg3_token %u leg3_strike %u leg3_lotsize %u leg3_pro %d leg3_type %d leg3_side %d "
                                                       "leg4_token %u leg4_strike %u leg4_lotsize %u leg4_pro %d leg4_type %d leg4_side %d",
                                                       key.c_str(),
                                                       p.portfolio_id, (int)p.kind, (int)p.leg_count, p.is_active, p.terminate,
                                                       p.is_iter_over, p.stop_requested, (int)p.stale_status, p.traded_qty, p.achieved_spread,
                                                       p.legs[0].symbol_token, p.legs[0].strike_price, p.legs[0].lot_size,
                                                       p.legs[0].is_pro_account, (int)p.legs[0].leg_type, (int)p.legs[0].side,
                                                       p.legs[1].symbol_token, p.legs[1].strike_price, p.legs[1].lot_size,
                                                       p.legs[1].is_pro_account, (int)p.legs[1].leg_type, (int)p.legs[1].side,
                                                       p.legs[2].symbol_token, p.legs[2].strike_price, p.legs[2].lot_size,
                                                       p.legs[2].is_pro_account, (int)p.legs[2].leg_type, (int)p.legs[2].side,
                                                       p.legs[3].symbol_token, p.legs[3].strike_price, p.legs[3].lot_size,
                                                       p.legs[3].is_pro_account, (int)p.legs[3].leg_type, (int)p.legs[3].side);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error Portfolio" << std::endl;
        }
        else
        {
            // Add to portfolio index
            redisCommand(ctx, "SADD portfolios %d", p.portfolio_id);

            LogMsg m{};
            m.portfolio_id = p.portfolio_id;
            snprintf(m.text, sizeof(m.text),
                     "%s [T%lu] Inserted PortfolioID=%d Spread=%d Qty=%d",
                     now_str().c_str(), tid(), p.portfolio_id, p.achieved_spread, p.traded_qty);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::ThreeLegIOCParams:
    {
        std::cout << "ThreeLegIOCParams\n";
        const ConRevParams &p = msg.three_leg_ioc_params;

        std::string key = "conrev_params:" + std::to_string(p.portfolio_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s portfolio_id %d con_flag %d max_lots %d sol %d "
                                                       "spread %.6f opp_check %d diff %.6f timer %.6f",
                                                       key.c_str(), p.portfolio_id, p.con_flag, p.max_lots, p.sol,
                                                       p.spread, p.opp_check, p.diff, p.timer);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error ConRevParams" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD conrev_params_list %d", p.portfolio_id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::ThreeLegIOCOrderData:
    {
        std::cout << "ThreeLegIOCData\n";
        const ConRevOrderData &d = msg.three_leg_ioc_data;

        std::string key = "conrev_order:" + std::to_string(d.portfolio_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s portfolio_id %d traded_qty %u ordered_qty %u remain_qty %u "
                                                       "achieved_spread %d leg1_oms_id %u leg1_filled %d leg1_order_id %u "
                                                       "leg2_order_id %u leg3_order_id %u fut_token %u call_token %u put_token %u "
                                                       "fut_price %u call_price %u put_price %u v1_fut %lu v2_call %lu v3_put %lu "
                                                       "q1_fut %u q2_call %u q3_put %u price_fut %u price_call %u price_put %u "
                                                       "strike_price %d con_flag %d",
                                                       key.c_str(), d.portfolio_id, d.traded_qty, d.ordered_qty, d.remain_qty,
                                                       d.achieved_spread, d.leg1_oms_id, d.leg1_filled, d.leg1_order_id,
                                                       d.leg2_order_id, d.leg3_order_id, d.fut_token, d.call_token, d.put_token,
                                                       d.fut_price, d.call_price, d.put_price, d.v1_fut, d.v2_call, d.v3_put,
                                                       d.q1_fut, d.q2_call, d.q3_put, d.price_fut, d.price_call, d.price_put,
                                                       d.strike_price, d.con_flag);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error ConRevOrderData" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD conrev_orders_list %d", d.portfolio_id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::ThreeLegBiddingParams:
    {
        std::cout << "ThreeLegBiddingParams\n";
        const ThreeLegBiddingParams &p = msg.three_leg_bidding_params;

        std::string key = "three_leg_params:" + std::to_string(p.portfolio_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s portfolio_id %d con_flag %d max_lots %d sol %d "
                                                       "spread %.6f leg1_spread_threshold %d "
                                                       "legs2_timeout_us %lu legs3_timeout_us %lu is_opportunity %d",
                                                       key.c_str(), p.portfolio_id, p.con_flag, p.max_lots, p.sol,
                                                       p.spread, p.leg1_spread_threshold,
                                                       p.legs2_timeout_us, p.legs3_timeout_us, p.is_opportunity);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error ThreeLegBiddingParams" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD three_leg_params_list %d", p.portfolio_id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::ThreeLegBiddingOrderData:
    {
        std::cout << "ThreeLegBiddingOrderData\n";
        const ThreeLegBiddingOrderData &o = msg.three_leg_bidding_data;

        std::string key = "three_leg_order:" + std::to_string(o.portfolio_id);

        // Split into multiple commands due to Redis command size limits
        redisReply *reply1 = (redisReply *)redisCommand(ctx,
                                                        "HMSET %s portfolio_id %d leg1_over_all_fill %d leg2_over_all_fill %d "
                                                        "leg3_over_all_fill %d leg2_covered_qty %d leg3_covered_qty %d "
                                                        "leg2_pending_qty %d leg3_pending_qty %d state %d is_conversion %d",
                                                        key.c_str(), o.portfolio_id, o.leg1_over_all_fill, o.leg2_over_all_fill,
                                                        o.leg3_over_all_fill, o.leg2_covered_qty, o.leg3_covered_qty,
                                                        o.leg2_pending_qty, o.leg3_pending_qty, (int)o.state, o.is_conversion);

        redisReply *reply2 = (redisReply *)redisCommand(ctx,
                                                        "HMSET %s last_leg1_price %d last_qty_leg1 %d last_qty_leg2 %d "
                                                        "last_covered_leg2 %d last_qty_leg3 %d last_covered_leg3 %d "
                                                        "last_price_leg1 %d last_price_leg2 %d last_price_leg3 %d",
                                                        key.c_str(), o.last_leg1_price, o.last_qty_leg1, o.last_qty_leg2,
                                                        o.last_covered_leg2, o.last_qty_leg3, o.last_covered_leg3,
                                                        o.last_price_leg1, o.last_price_leg2, o.last_price_leg3);

        redisReply *reply3 = (redisReply *)redisCommand(ctx,
                                                        "HMSET %s leg1_order_id %d leg2_order_id %d leg3_order_id %d "
                                                        "leg1_ack %d leg2_ack %d leg3_ack %d leg1_cancel_request %d "
                                                        "traded_qty %d current_cycle_qty %d current_hedge_qty %d",
                                                        key.c_str(), o.leg1_order_id, o.leg2_order_id, o.leg3_order_id,
                                                        o.leg1_ack, o.leg2_ack, o.leg3_ack, o.leg1_cancel_request,
                                                        o.traded_qty, o.current_cycle_qty, o.current_hedge_qty);

        redisReply *reply4 = (redisReply *)redisCommand(ctx,
                                                        "HMSET %s v1_fut %lu v2_call %lu v3_put %lu q1_fut %d q2_call %d q3_put %d "
                                                        "price_fut %d price_call %d price_put %d strike_price %d achieved_spread %ld "
                                                        "bid_traded_qty %d bid_ordered_qty %d new_max %d mod_max %d current_cycle_traded %d",
                                                        key.c_str(), o.v1_fut, o.v2_call, o.v3_put, o.q1_fut, o.q2_call, o.q3_put,
                                                        o.price_fut, o.price_call, o.price_put, o.strike_price, o.achieved_spread,
                                                        o.bid_traded_qty, o.bid_ordered_qty, o.new_max, o.mod_max, o.current_cycle_traded);

        bool success = true;
        if (!reply1 || reply1->type == REDIS_REPLY_ERROR)
            success = false;
        if (!reply2 || reply2->type == REDIS_REPLY_ERROR)
            success = false;
        if (!reply3 || reply3->type == REDIS_REPLY_ERROR)
            success = false;
        if (!reply4 || reply4->type == REDIS_REPLY_ERROR)
            success = false;

        if (!success)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error ThreeLegBiddingOrderData" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD three_leg_orders_list %d", o.portfolio_id);
        }

        if (reply1)
            freeReplyObject(reply1);
        if (reply2)
            freeReplyObject(reply2);
        if (reply3)
            freeReplyObject(reply3);
        if (reply4)
            freeReplyObject(reply4);
        break;
    }

    case ShmMessageType::BoxIOCParams:
    {
        std::cout << "BoxIocParams\n";
        const BoxIocParams &p = msg.box_ioc_params;

        // Prefer an external identifier from your message envelope, e.g. portfolio_id
        // Replace `msg.portfolio_id` with your actual identifier field.
        uint32_t id = p.portfolio_id;
        std::string key = "boxioc_params:" + std::to_string(id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s "
                                                       "portfolio_id %u "
                                                       "call_itm_token %u put_otm_token %u call_otm_token %u put_itm_token %u "
                                                       "price_difference %lld is_flip_box %d "
                                                       "max_lots %u sol %u timer_ms %u",
                                                       key.c_str(),
                                                       id,
                                                       p.call_itm_token, p.put_otm_token, p.call_otm_token, p.put_itm_token,
                                                       (long long)p.price_difference, (int)p.is_flip_box,
                                                       p.max_lots, p.sol, p.timer_ms);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error BoxIocParams" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD boxioc_params_list %u", id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::BoxIOCOrderData:
    {
        std::cout << "BoxIocOrderData\n";
        const BoxIocOrderData &d = msg.box_ioc_data;

        // Again, prefer an external ID
        uint32_t id = d.portfolio_id;
        std::string key = "boxioc_order:" + std::to_string(id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s "
                                                       "portfolio_id %u "
                                                       "state %d is_flip_box %d "
                                                       "call_itm_order_id %u put_itm_order_id %u "
                                                       "call_itm_pending_qty %u put_itm_pending_qty %u "
                                                       "call_itm_filled_qty %u put_itm_filled_qty %u "
                                                       "call_itm_ack %d put_itm_ack %d "
                                                       "call_itm_cancelled %d put_itm_cancelled %d "
                                                       "call_itm_filled %d put_itm_filled %d "
                                                       "second_leg_order_id %u second_leg_pending_qty %u second_leg_filled_qty %u "
                                                       "second_leg_price %u second_leg_depth %u second_leg_counter %u "
                                                       "second_leg_ack %d second_leg_timer_identifier %d last_quantity_second_leg %u "
                                                       "third_leg_order_id %u third_leg_pending_qty %u third_leg_filled_qty %u "
                                                       "third_leg_price %u third_leg_depth %u third_leg_counter %u "
                                                       "third_leg_ack %d third_leg_timer_identifier %d last_quantity_third_leg %u "
                                                       "itm_timer_start %llu otm_timer_start %llu second_otm_timer_start %llu third_otm_timer_start %llu "
                                                       "timer_value %u strike_difference %lld current_spread %lld "
                                                       "traded_qty %u "
                                                       "first_leg_trade %d second_leg_trade %d place_second_order %d place_aggressive %d "
                                                       "modify_flag %d terminate_check %d",
                                                       key.c_str(),
                                                       id,
                                                       (int)d.state, (int)d.is_flip_box,
                                                       d.call_itm_order_id, d.put_itm_order_id,
                                                       d.call_itm_pending_qty, d.put_itm_pending_qty,
                                                       d.call_itm_filled_qty, d.put_itm_filled_qty,
                                                       (int)d.call_itm_ack, (int)d.put_itm_ack,
                                                       (int)d.call_itm_cancelled, (int)d.put_itm_cancelled,
                                                       (int)d.call_itm_filled, (int)d.put_itm_filled,
                                                       d.second_leg_order_id, d.second_leg_pending_qty, d.second_leg_filled_qty,
                                                       d.second_leg_price, d.second_leg_depth, d.second_leg_counter,
                                                       (int)d.second_leg_ack, (int)d.second_leg_timer_identifier, d.last_quantity_second_leg,
                                                       d.third_leg_order_id, d.third_leg_pending_qty, d.third_leg_filled_qty,
                                                       d.third_leg_price, d.third_leg_depth, d.third_leg_counter,
                                                       (int)d.third_leg_ack, (int)d.third_leg_timer_identifier, d.last_quantity_third_leg,
                                                       (unsigned long long)d.itm_timer_start,
                                                       (unsigned long long)d.otm_timer_start,
                                                       (unsigned long long)d.second_otm_timer_start,
                                                       (unsigned long long)d.third_otm_timer_start,
                                                       d.timer_value,
                                                       (long long)d.strike_difference, (long long)d.current_spread,
                                                       d.traded_qty,
                                                       (int)d.first_leg_trade, (int)d.second_leg_trade, (int)d.place_second_order, (int)d.place_aggressive,
                                                       (int)d.modify_flag, (int)d.terminate_check);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error BoxIocOrderData" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD boxioc_orders_list %u", id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::BoxBiddingOrderData:
    {
        std::cout << "BoxBiddingOrderData\n";
        const BoxBiddingOrderData &o = msg.box_bidding_data;

        std::string key = "box_bidding_order_data:" + std::to_string(o.portfolio_id);

        std::cout << "Box Order state for this tick: " << static_cast<int>(msg.box_bidding_data.state) << " for pf id: " << msg.box_bidding_data.portfolio_id << std::endl;

        redisReply *reply = (redisReply *)redisCommand(
            ctx,
            "HMSET %s "
            // primary
            "portfolio_id %d "

            // tokens
            "itm_call_token %u "
            "itm_put_token %u "
            "otm_call_token %u "
            "otm_put_token %u "

            // covered / pending qty
            "leg2_covered_qty %u "
            "leg3_covered_qty %u "
            "leg4_covered_qty %u "
            "leg2_pending_qty %u "
            "leg3_pending_qty %u "
            "leg4_pending_qty %u "

            // entry + state
            "entry_leg %u "
            "state %d "
            "is_flip %d "

            // order ids
            "leg1_order_id %d "
            "leg2_order_id %d "
            "leg3_order_id %d "
            "leg4_order_id %d "

            // acks
            "leg1_ack %d "
            "leg2_ack %d "
            "leg3_ack %d "
            "leg4_ack %d "

            // qty tracking
            "traded_qty %u "
            "current_cycle_qty %u "
            "current_hedge_qty %u "

            // leg1
            "leg1_filled %d "
            "leg1_partial_filled %d "
            "leg1_pending_qty %u "
            "leg1_filled_qty %u "
            "leg1_price %u "
            "leg1_timer_start %lu "
            "last_leg1_qty %u "
            "last_leg1_price %u "

            // leg2
            "leg2_filled %d "
            "leg2_filled_qty %u "
            "leg2_counter %u "
            "leg2_depth %u "
            "last_leg2_price %u "
            "leg2_price %u "
            "last_leg2_qty %u "
            "last_covered_leg2 %u "
            "legs2_timer_start %lu "
            "legs2_level %u "
            "leg2_level2 %d "
            "leg2_actual_fill_qty %u "

            // leg3
            "leg3_filled %d "
            "leg3_filled_qty %u "
            "leg3_counter %u "
            "leg3_depth %u "
            "last_leg3_price %u "
            "leg3_price %u "
            "last_leg3_qty %u "
            "last_covered_leg3 %u "
            "legs3_timer_start %lu "
            "legs3_level %u "
            "leg3_level2 %d "
            "leg3_actual_fill_qty %u "

            // leg4
            "leg4_filled %d "
            "leg4_filled_qty %u "
            "leg4_counter %u "
            "leg4_depth %u "
            "last_leg4_price %u "
            "leg4_price %u "
            "last_leg4_qty %u "
            "last_covered_leg4 %u "
            "legs4_timer_start %lu "
            "legs4_level %u "
            "leg4_level2 %d "
            "leg4_actual_fill_qty %u "

            // pnl
            "v1 %u q1 %u v2 %u q2 %u v3 %u q3 %u v4 %u q4 %u "

            // calculated
            "price_leg1_for_entire_cycle %u "
            "price_leg2_for_entire_cycle %u "
            "price_leg3_for_entire_cycle %u "
            "price_leg4_for_entire_cycle %u "
            "strike_diff %u "
            "achieved_spread %lld "

            // legacy
            "bid_traded_qty %u "
            "bid_ordered_qty %u "
            "new_max %u "
            "mod_max %u "

            // rejections
            "modify_reject %d "
            "cancel_reject %d",

            key.c_str(),

            // primary
            o.portfolio_id,

            // tokens
            o.itm_call_token, o.itm_put_token, o.otm_call_token, o.otm_put_token,

            // covered/pending
            o.leg2_covered_qty, o.leg3_covered_qty, o.leg4_covered_qty,
            o.leg2_pending_qty, o.leg3_pending_qty, o.leg4_pending_qty,

            // entry/state
            o.entry_leg,
            (int)o.state,
            o.is_flip,

            // order ids
            o.leg1_order_id, o.leg2_order_id, o.leg3_order_id, o.leg4_order_id,

            // acks
            o.leg1_ack, o.leg2_ack, o.leg3_ack, o.leg4_ack,

            // qtys
            o.traded_qty, o.current_cycle_qty, o.current_hedge_qty,

            // leg1
            o.leg1_filled, o.leg1_partial_filled, o.leg1_pending_qty,
            o.leg1_filled_qty, o.leg1_price, o.leg1_timer_start,
            o.last_leg1_qty, o.last_leg1_price,

            // leg2
            o.leg2_filled, o.leg2_filled_qty, o.leg2_counter, o.leg2_depth,
            o.last_leg2_price, o.leg2_price, o.last_leg2_qty, o.last_covered_leg2,
            o.legs2_timer_start, o.legs2_level, o.leg2_level2, o.leg2_actual_fill_qty,

            // leg3
            o.leg3_filled, o.leg3_filled_qty, o.leg3_counter, o.leg3_depth,
            o.last_leg3_price, o.leg3_price, o.last_leg3_qty, o.last_covered_leg3,
            o.legs3_timer_start, o.legs3_level, o.leg3_level2, o.leg3_actual_fill_qty,

            // leg4
            o.leg4_filled, o.leg4_filled_qty, o.leg4_counter, o.leg4_depth,
            o.last_leg4_price, o.leg4_price, o.last_leg4_qty, o.last_covered_leg4,
            o.legs4_timer_start, o.legs4_level, o.leg4_level2, o.leg4_actual_fill_qty,

            // pnl
            o.v1, o.q1, o.v2, o.q2, o.v3, o.q3, o.v4, o.q4,

            // calculated
            o.price_leg1_for_entire_cycle,
            o.price_leg2_for_entire_cycle,
            o.price_leg3_for_entire_cycle,
            o.price_leg4_for_entire_cycle,
            o.strike_diff,
            (long long)o.achieved_spread,

            // legacy
            o.bid_traded_qty, o.bid_ordered_qty, o.new_max, o.mod_max,

            // rejections
            o.modify_reject, o.cancel_reject

        );

        bool success = true;
        if (!reply || reply->type == REDIS_REPLY_ERROR)
            success = false;

        if (!success)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error BoxBiddingOrderData" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD box_bidding_orders_list %d", o.portfolio_id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::BoxBiddingParams:
    {
        std::cout << "BoxBiddingParams\n";
        const BoxBiddingParams &p = msg.box_bidding_params;

        std::string key = "box_bidding_params:" + std::to_string(p.portfolio_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s "
                                                       "portfolio_id %d "
                                                       "max_lots %u "
                                                       "sol %u "
                                                       "flip_box_enabled %d "
                                                       "price_difference %lld "
                                                       "flip_price_difference %lld "
                                                       "leg1_spread_threshold %d "
                                                       "legs2_timeout_us %lu "
                                                       "legs3_timeout_us %lu "
                                                       "legs4_timeout_us %lu "
                                                       "leg1_timeout_us %lu "
                                                       "entry_leg %u "
                                                       "is_opportunity %d",
                                                       key.c_str(),
                                                       p.portfolio_id,
                                                       p.max_lots,
                                                       p.sol,
                                                       p.flip_box_enabled ? 1 : 0,
                                                       (long long)p.price_difference,
                                                       (long long)p.flip_price_difference,
                                                       p.leg1_spread_threshold,
                                                       p.legs2_timeout_us,
                                                       p.legs3_timeout_us,
                                                       p.legs4_timeout_us,
                                                       p.leg1_timeout_us,
                                                       p.entry_leg,
                                                       p.is_opportunity ? 1 : 0);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error BoxBiddingParams" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD box_bidding_params_list %d", p.portfolio_id);
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::StrategyLegData:
    {
        std::cout << "StrategyLegData\n";
        const StrategyLegData &s = msg.oms;

        std::string key = "strategy_leg:" + std::to_string(s.oms_order_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s oms_order_id %u token %u side %d portfolio_id %u "
                                                       "fill_price_sum %u fill_qty_sum %u required_qty %u "
                                                       "exchange_order_id %lu exchange_modified_time %lu order_state %d",
                                                       key.c_str(), s.oms_order_id, s.token, (int)s.side, s.portfolio_id,
                                                       s.fill_price_sum, s.fill_qty_sum, s.required_qty,
                                                       s.exchange_order_id, s.exchange_modified_time, (int)s.order_state);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error StrategyLegData" << std::endl;
        }
        else
        {
            redisCommand(ctx, "SADD strategy_legs_list %u", s.oms_order_id);

            // If filled, also store trade
            if (s.order_state == OrderState::Fill || s.order_state == OrderState::PartialFill)
            {
                std::string trade_key = "trade:" + std::to_string(s.exchange_modified_time) + ":" + std::to_string(s.oms_order_id);
                redisCommand(ctx,
                             "HMSET %s oms_order_id %u portfolio_id %u fill_qty %u fill_price %u "
                             "exchange_order_id %lu timestamp %lu partial_fill %d",
                             trade_key.c_str(), s.oms_order_id, s.portfolio_id, s.fill_qty_sum, s.fill_price_sum,
                             s.exchange_order_id, s.exchange_modified_time,
                             (s.order_state == OrderState::PartialFill) ? 1 : 0);

                redisCommand(ctx, "ZADD trades %lu %s", s.exchange_modified_time, trade_key.c_str());
            }
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::Trade:
    {
        std::cout << "Trades\n";
        const Trade &t = msg.trade;

        std::string key = "trade:" + std::to_string(t.timestamp) + ":" + std::to_string(t.oms_order_id);

        redisReply *reply = (redisReply *)redisCommand(ctx,
                                                       "HMSET %s oms_order_id %u fill_qty %u fill_price %u "
                                                       "exchange_order_id %lu timestamp %lu partial_fill %d",
                                                       key.c_str(), t.oms_order_id, t.fill_qty, t.fill_price,
                                                       t.exchange_order_id, t.timestamp, t.partial_fill);

        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis Error Trade" << std::endl;
        }
        else
        {
            // Add to sorted set for time-based queries
            redisCommand(ctx, "ZADD trades %lu %s", t.timestamp, key.c_str());
        }

        if (reply)
            freeReplyObject(reply);
        break;
    }

    case ShmMessageType::LogMessage:
    {

        const LogMessageShm &log = msg.log_shm;
        // log.is_utrade
        // log.msg_type
        std::ofstream ofs("packet_types.txt", std::ios::app);
        if (!ofs) {
            std::cerr << "Failed to open packet_types.txt\n";
        } else {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);

            // format time as YYYY-MM-DD HH:MM:SS
            ofs << "[" << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << "] "
                << "is_utrade: " << log.is_utrade
                << ", msg_type: " << static_cast<int>(log.msg_type)
                << std::endl;

        }
        break;
    }

    default:
        std::cerr << now_str() << " [T" << tid() << "] Unknown message type: " << static_cast<int>(msg.type) << "\n";
        break;
    }
}

// ============================================================================
// DB WRITER THREAD
// ============================================================================
inline void db_writer(DBQueue *q, const char *host, int port, int redis_db_index)
{
    LogQueue *lq;
    redisContext *ctx = redisConnect(host, port);

    if (ctx == NULL || ctx->err)
    {
        if (ctx)
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis connect failed: " << ctx->errstr << std::endl;
            redisFree(ctx);
        }
        else
        {
            std::cerr << now_str() << " [T" << tid() << "] Redis connect failed: can't allocate context" << std::endl;
        }
        return;
    }

    std::cout << now_str() << " [T" << tid() << "] Connected to Redis.\n";

    // Build the SELECT command using the argument
    redisReply *reply = (redisReply *)redisCommand(ctx, "SELECT %d", redis_db_index);

    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr << "Failed to select database " << redis_db_index << ": "
                  << (reply ? reply->str : "NULL reply") << std::endl;
        if (reply)
            freeReplyObject(reply);
        return;
    }

    std::cout << "Connected to Redis database " << redis_db_index << std::endl;
    freeReplyObject(reply);

    //  Set app name key
    redisReply *app_reply = (redisReply *)redisCommand(ctx, "SET app:name platformtest");
    if (app_reply)
    {
        std::cout << "Set app:name = platformtest" << std::endl;
        freeReplyObject(app_reply);
    }

    ShmMessage msg{};
    int counter = 0;

    while (true)
    {
        if (!q->pop(msg))
        {
            _mm_pause();
            continue;
        }

        handleShmMessage(msg, ctx, lq);

        // Redis doesn't need explicit commits, but we can use SAVE periodically
        // if (++counter % 100 == 0)
        // {
        //     redisCommand(ctx, "BGSAVE");
        // }
    }

    redisFree(ctx);
}

// ============================================================================
// DB WRITER PROCESS
// ============================================================================
inline void db_writer_process(const char *host, int port, const char *shm_db_name, int redis_db_index)
{

    auto *db_q = create_or_attach_shm<DBQueue>(shm_db_name, false);

    std::thread t_db(db_writer, db_q, host, port, redis_db_index);
    t_db.join();
}

inline bool check_db(redisContext *ctx, int redis_db_index)
{

    // Build the SELECT command using the argument
    redisReply *reply = (redisReply *)redisCommand(ctx, "SELECT %d", redis_db_index);

    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr << "Failed to select database " << redis_db_index << ": "
                  << (reply ? reply->str : "NULL reply") << std::endl;
        if (reply)
            freeReplyObject(reply);
        return false;
    }

    std::cout << "Connected to Redis database " << redis_db_index << std::endl;
    freeReplyObject(reply);

    // verify app name
    redisReply *app_check = (redisReply *)redisCommand(ctx, "GET app:name");
    if (app_check && app_check->type == REDIS_REPLY_STRING)
    {
        std::cout << "App name: " << app_check->str << std::endl;
        if (std::string(app_check->str) != "platformtest")
        {
            std::cerr << "Warning: app:name mismatch. Expected 'platformtest', got '" << app_check->str << "'" << std::endl;
            return false;
        }
    }
    if (app_check)
        freeReplyObject(app_check);
    return true;
}

// ============================================================================
// RECOVERY FUNCTIONS
// ============================================================================
inline void recover_portfolios(const char *host, int port,
                               Portfolio (&portfolios)[MAX_PORTFOLIOS],
                               ska::flat_hash_map<uint32_t, ska::flat_hash_set<uint16_t>> &token_to_portfolios,
                               bool (&portfolio_initialized)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed: " << (ctx ? ctx->errstr : "NULL context") << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    // Get all portfolio IDs
    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS portfolios");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        std::cerr << "Failed to get portfolio list" << std::endl;
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::string key = "portfolio:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            PortfolioShm p{};

            // Parse hash fields
            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "portfolio_id")
                    p.portfolio_id = std::stoi(value);
                else if (field == "kind")
                    p.kind = static_cast<StrategyKind>(std::stoi(value));
                else if (field == "leg_count")
                    p.leg_count = std::stoi(value);
                else if (field == "is_active")
                    p.is_active = std::stoi(value);
                else if (field == "terminate")
                    p.terminate = std::stoi(value);
                else if (field == "is_iter_over")
                    p.is_iter_over = std::stoi(value);
                else if (field == "stop_requested")
                    p.stop_requested = std::stoi(value);
                else if (field == "stale_status")
                    p.stale_status = static_cast<StaleStatus>(std::stoi(value));
                else if (field == "traded_qty")
                    p.traded_qty = std::stoul(value);
                else if (field == "achieved_spread")
                    p.achieved_spread = std::stoi(value);
                // Leg 1
                else if (field == "leg1_token")
                    p.legs[0].symbol_token = std::stoul(value);
                else if (field == "leg1_strike")
                    p.legs[0].strike_price = std::stoul(value);
                else if (field == "leg1_lotsize")
                    p.legs[0].lot_size = std::stoul(value);
                else if (field == "leg1_pro")
                    p.legs[0].is_pro_account = std::stoi(value);
                else if (field == "leg1_type")
                    p.legs[0].leg_type = static_cast<LegType>(std::stoi(value));
                else if (field == "leg1_side")
                    p.legs[0].side = static_cast<OrderSide>(std::stoi(value));
                // Leg 2
                else if (field == "leg2_token")
                    p.legs[1].symbol_token = std::stoul(value);
                else if (field == "leg2_strike")
                    p.legs[1].strike_price = std::stoul(value);
                else if (field == "leg2_lotsize")
                    p.legs[1].lot_size = std::stoul(value);
                else if (field == "leg2_pro")
                    p.legs[1].is_pro_account = std::stoi(value);
                else if (field == "leg2_type")
                    p.legs[1].leg_type = static_cast<LegType>(std::stoi(value));
                else if (field == "leg2_side")
                    p.legs[1].side = static_cast<OrderSide>(std::stoi(value));
                // Leg 3
                else if (field == "leg3_token")
                    p.legs[2].symbol_token = std::stoul(value);
                else if (field == "leg3_strike")
                    p.legs[2].strike_price = std::stoul(value);
                else if (field == "leg3_lotsize")
                    p.legs[2].lot_size = std::stoul(value);
                else if (field == "leg3_pro")
                    p.legs[2].is_pro_account = std::stoi(value);
                else if (field == "leg3_type")
                    p.legs[2].leg_type = static_cast<LegType>(std::stoi(value));
                else if (field == "leg3_side")
                    p.legs[2].side = static_cast<OrderSide>(std::stoi(value));
                // Leg 4
                else if (field == "leg4_token")
                    p.legs[3].symbol_token = std::stoul(value);
                else if (field == "leg4_strike")
                    p.legs[3].strike_price = std::stoul(value);
                else if (field == "leg4_lotsize")
                    p.legs[3].lot_size = std::stoul(value);
                else if (field == "leg4_pro")
                    p.legs[3].is_pro_account = std::stoi(value);
                else if (field == "leg4_type")
                    p.legs[3].leg_type = static_cast<LegType>(std::stoi(value));
                else if (field == "leg4_side")
                    p.legs[3].side = static_cast<OrderSide>(std::stoi(value));
            }

            // Register tokens
            for (int j = 0; j < p.leg_count; ++j)
            {
                token_to_portfolios[p.legs[j].symbol_token].insert(p.portfolio_id);
            }

            if (p.portfolio_id < MAX_PORTFOLIOS)
            {
                Portfolio &dst = portfolios[p.portfolio_id];
                portfolio_initialized[p.portfolio_id] = true;
                dst.portfolio_id = p.portfolio_id;
                dst.kind = p.kind;
                dst.leg_count = p.leg_count;
                dst.is_active = p.is_active;
                dst.terminate = p.terminate;
                dst.is_iter_over = p.is_iter_over;
                dst.stop_requested = p.stop_requested;
                dst.stale_status = p.stale_status;
                dst.traded_qty = p.traded_qty;
                dst.achieved_spread = p.achieved_spread;

                dst.updated_tick = false;
                dst.ordered_flag = false;
                dst.is_data_updated = false;

                for (int j = 0; j < 4; ++j)
                {
                    dst.legs[j] = p.legs[j];
                }

                std::cout << "Recovered Portfolio ID: " << p.portfolio_id << "\n";
            }
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_three_leg_params(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }
    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS three_leg_params_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::string key = "three_leg_params:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            ThreeLegBiddingParams p{};
            p.portfolio_id = pf_id;

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "con_flag")
                    p.con_flag = std::stoi(value);
                else if (field == "max_lots")
                    p.max_lots = std::stoi(value);
                else if (field == "sol")
                    p.sol = std::stoi(value);
                else if (field == "spread")
                    p.spread = std::stod(value);
                else if (field == "leg1_spread_threshold")
                    p.leg1_spread_threshold = std::stoi(value);
                else if (field == "legs2_timeout_us")
                    p.legs2_timeout_us = std::stoull(value);
                else if (field == "legs3_timeout_us")
                    p.legs3_timeout_us = std::stoull(value);
                else if (field == "is_opportunity")
                    p.is_opportunity = std::stoi(value);
            }

            portfolios[pf_id].params.three_leg_bidding = p;
            std::cout << "Recovered ThreeLeg Params for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_three_leg_orders(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS three_leg_orders_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::string key = "three_leg_order:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            ThreeLegBiddingOrderData o{};
            o.portfolio_id = pf_id;

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "leg1_over_all_fill")
                    o.leg1_over_all_fill = std::stoul(value);
                else if (field == "leg2_over_all_fill")
                    o.leg2_over_all_fill = std::stoul(value);
                else if (field == "leg3_over_all_fill")
                    o.leg3_over_all_fill = std::stoul(value);
                else if (field == "leg2_covered_qty")
                    o.leg2_covered_qty = std::stoul(value);
                else if (field == "leg3_covered_qty")
                    o.leg3_covered_qty = std::stoul(value);
                else if (field == "leg2_pending_qty")
                    o.leg2_pending_qty = std::stoul(value);
                else if (field == "leg3_pending_qty")
                    o.leg3_pending_qty = std::stoul(value);
                else if (field == "state")
                    o.state = static_cast<ThreeLegBiddingState>(std::stoi(value));
                else if (field == "is_conversion")
                    o.is_conversion = std::stoi(value);
                else if (field == "traded_qty")
                    o.traded_qty = std::stoul(value);
                else if (field == "achieved_spread")
                    o.achieved_spread = std::stoll(value);
                else if (field == "leg1_order_id")
                    o.leg1_order_id = std::stoi(value);
                else if (field == "leg2_order_id")
                    o.leg2_order_id = std::stoi(value);
                else if (field == "leg3_order_id")
                    o.leg3_order_id = std::stoi(value);
                else if (field == "leg1_ack")
                    o.leg1_ack = std::stoi(value);
                else if (field == "leg2_ack")
                    o.leg2_ack = std::stoi(value);
                else if (field == "leg3_ack")
                    o.leg3_ack = std::stoi(value);
                else if (field == "v1_fut")
                    o.v1_fut = std::stoull(value);
                else if (field == "v2_call")
                    o.v2_call = std::stoull(value);
                else if (field == "v3_put")
                    o.v3_put = std::stoull(value);
                else if (field == "price_fut")
                    o.price_fut = std::stoul(value);
                else if (field == "price_call")
                    o.price_call = std::stoul(value);
                else if (field == "price_put")
                    o.price_put = std::stoul(value);
                else if (field == "strike_price")
                    o.strike_price = std::stoul(value);
                // Add more fields as needed...
            }

            portfolios[pf_id].order_data.three_leg_bidding = o;
            std::cout << "Recovered ThreeLeg Order for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_conrev_params(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }
    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS conrev_params_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::string key = "conrev_params:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            ConRevParams p{};
            p.portfolio_id = pf_id;

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "con_flag")
                    p.con_flag = std::stoi(value);
                else if (field == "max_lots")
                    p.max_lots = std::stoi(value);
                else if (field == "sol")
                    p.sol = std::stoi(value);
                else if (field == "spread")
                    p.spread = std::stod(value);
                else if (field == "opp_check")
                    p.opp_check = std::stoi(value);
                else if (field == "diff")
                    p.diff = std::stod(value);
                else if (field == "timer")
                    p.timer = std::stod(value);
            }

            portfolios[pf_id].params.conrev = p;
            std::cout << "Recovered ConRev Params for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_conrev_order_data(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS conrev_orders_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::string key = "conrev_order:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            ConRevOrderData d{};
            d.portfolio_id = pf_id;

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "traded_qty")
                    d.traded_qty = std::stoul(value);
                else if (field == "ordered_qty")
                    d.ordered_qty = std::stoul(value);
                else if (field == "remain_qty")
                    d.remain_qty = std::stoul(value);
                else if (field == "achieved_spread")
                    d.achieved_spread = std::stoi(value);
                else if (field == "leg1_oms_id")
                    d.leg1_oms_id = std::stoul(value);
                else if (field == "leg1_filled")
                    d.leg1_filled = std::stoi(value);
                else if (field == "fut_token")
                    d.fut_token = std::stoul(value);
                else if (field == "call_token")
                    d.call_token = std::stoul(value);
                else if (field == "put_token")
                    d.put_token = std::stoul(value);
                else if (field == "fut_price")
                    d.fut_price = std::stoul(value);
                else if (field == "call_price")
                    d.call_price = std::stoul(value);
                else if (field == "put_price")
                    d.put_price = std::stoul(value);
                else if (field == "strike_price")
                    d.strike_price = std::stoi(value);
                else if (field == "con_flag")
                    d.con_flag = std::stoi(value);
                // Add more fields as needed...
            }

            portfolios[pf_id].order_data.conrev = d;
            std::cout << "Recovered ConRev Order for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_box_bidding_params(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS box_bidding_params_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    std::cout << "Box Bid param reply ele size: " << reply->elements << '\n';

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::cout << "going through pf id: " << pf_id;
        std::string key = "box_bidding_params:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            BoxBiddingParams p{};

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "max_lots")
                    p.max_lots = std::stoul(value);
                else if (field == "sol")
                    p.sol = std::stoul(value);
                else if (field == "flip_box_enabled")
                    p.flip_box_enabled = std::stoi(value);
                else if (field == "price_difference")
                    p.price_difference = std::stoll(value);
                else if (field == "flip_price_difference")
                    p.flip_price_difference = std::stoll(value);
                else if (field == "leg1_spread_threshold")
                    p.leg1_spread_threshold = std::stoi(value);
                else if (field == "legs2_timeout_us")
                    p.legs2_timeout_us = std::stoull(value);
                else if (field == "legs3_timeout_us")
                    p.legs3_timeout_us = std::stoull(value);
                else if (field == "legs4_timeout_us")
                    p.legs4_timeout_us = std::stoull(value);
                else if (field == "leg1_timeout_us")
                    p.leg1_timeout_us = std::stoull(value);
                else if (field == "entry_leg")
                    p.entry_leg = std::stoul(value);
                else if (field == "is_opportunity")
                    p.is_opportunity = std::stoi(value);
            }

            portfolios[pf_id].params.box_bidding = p;
            std::cout << "Recovered BoxBidding Params for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_box_bidding_orders(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS box_bidding_orders_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = std::stoi(reply->element[i]->str);
        std::string key = "box_bidding_order:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            BoxBiddingOrderData o{};
            o.portfolio_id = pf_id;

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                // tokens
                if (field == "itm_call_token")
                    o.itm_call_token = std::stoul(value);
                else if (field == "itm_put_token")
                    o.itm_put_token = std::stoul(value);
                else if (field == "otm_call_token")
                    o.otm_call_token = std::stoul(value);
                else if (field == "otm_put_token")
                    o.otm_put_token = std::stoul(value);

                // qty tracking
                else if (field == "leg2_covered_qty")
                    o.leg2_covered_qty = std::stoul(value);
                else if (field == "leg3_covered_qty")
                    o.leg3_covered_qty = std::stoul(value);
                else if (field == "leg4_covered_qty")
                    o.leg4_covered_qty = std::stoul(value);
                else if (field == "leg2_pending_qty")
                    o.leg2_pending_qty = std::stoul(value);
                else if (field == "leg3_pending_qty")
                    o.leg3_pending_qty = std::stoul(value);
                else if (field == "leg4_pending_qty")
                    o.leg4_pending_qty = std::stoul(value);

                // entry + state
                else if (field == "entry_leg")
                    o.entry_leg = std::stoul(value);
                else if (field == "state")
                    o.state = static_cast<BoxBiddingStates>(std::stoi(value));
                else if (field == "is_flip")
                    o.is_flip = std::stoi(value);

                // order IDs
                else if (field == "leg1_order_id")
                    o.leg1_order_id = std::stoi(value);
                else if (field == "leg2_order_id")
                    o.leg2_order_id = std::stoi(value);
                else if (field == "leg3_order_id")
                    o.leg3_order_id = std::stoi(value);
                else if (field == "leg4_order_id")
                    o.leg4_order_id = std::stoi(value);

                // leg enums
                else if (field == "leg1")
                    o.leg1 = static_cast<BoxBiddingLegs>(std::stoi(value));
                else if (field == "leg2")
                    o.leg2 = static_cast<BoxBiddingLegs>(std::stoi(value));
                else if (field == "leg3")
                    o.leg3 = static_cast<BoxBiddingLegs>(std::stoi(value));
                else if (field == "leg4")
                    o.leg4 = static_cast<BoxBiddingLegs>(std::stoi(value));

                // acks
                else if (field == "leg1_ack")
                    o.leg1_ack = std::stoi(value);
                else if (field == "leg2_ack")
                    o.leg2_ack = std::stoi(value);
                else if (field == "leg3_ack")
                    o.leg3_ack = std::stoi(value);
                else if (field == "leg4_ack")
                    o.leg4_ack = std::stoi(value);

                // quantities
                else if (field == "traded_qty")
                    o.traded_qty = std::stoul(value);
                else if (field == "current_cycle_qty")
                    o.current_cycle_qty = std::stoul(value);
                else if (field == "current_hedge_qty")
                    o.current_hedge_qty = std::stoul(value);

                // leg1 details
                else if (field == "leg1_filled")
                    o.leg1_filled = std::stoi(value);
                else if (field == "leg1_partial_filled")
                    o.leg1_partial_filled = std::stoi(value);
                else if (field == "leg1_pending_qty")
                    o.leg1_pending_qty = std::stoul(value);
                else if (field == "leg1_filled_qty")
                    o.leg1_filled_qty = std::stoul(value);
                else if (field == "leg1_price")
                    o.leg1_price = std::stoul(value);
                else if (field == "leg1_timer_start")
                    o.leg1_timer_start = std::stoull(value);
                else if (field == "last_leg1_qty")
                    o.last_leg1_qty = std::stoul(value);
                else if (field == "last_leg1_price")
                    o.last_leg1_price = std::stoul(value);

                // leg2 details
                else if (field == "leg2_filled")
                    o.leg2_filled = std::stoi(value);
                else if (field == "leg2_filled_qty")
                    o.leg2_filled_qty = std::stoul(value);
                else if (field == "leg2_counter")
                    o.leg2_counter = std::stoul(value);
                else if (field == "leg2_depth")
                    o.leg2_depth = std::stoul(value);
                else if (field == "leg2_price")
                    o.leg2_price = std::stoul(value);
                else if (field == "last_leg2_price")
                    o.last_leg2_price = std::stoul(value);
                else if (field == "last_leg2_qty")
                    o.last_leg2_qty = std::stoul(value);
                else if (field == "last_covered_leg2")
                    o.last_covered_leg2 = std::stoul(value);
                else if (field == "legs2_timer_start")
                    o.legs2_timer_start = std::stoull(value);
                else if (field == "legs2_level")
                    o.legs2_level = std::stoul(value);
                else if (field == "leg2_level2")
                    o.leg2_level2 = std::stoi(value);
                else if (field == "leg2_actual_fill_qty")
                    o.leg2_actual_fill_qty = std::stoul(value);

                // leg3 details
                else if (field == "leg3_filled")
                    o.leg3_filled = std::stoi(value);
                else if (field == "leg3_filled_qty")
                    o.leg3_filled_qty = std::stoul(value);
                else if (field == "leg3_counter")
                    o.leg3_counter = std::stoul(value);
                else if (field == "leg3_depth")
                    o.leg3_depth = std::stoul(value);
                else if (field == "leg3_price")
                    o.leg3_price = std::stoul(value);
                else if (field == "last_leg3_price")
                    o.last_leg3_price = std::stoul(value);
                else if (field == "last_leg3_qty")
                    o.last_leg3_qty = std::stoul(value);
                else if (field == "last_covered_leg3")
                    o.last_covered_leg3 = std::stoul(value);
                else if (field == "legs3_timer_start")
                    o.legs3_timer_start = std::stoull(value);
                else if (field == "legs3_level")
                    o.legs3_level = std::stoul(value);
                else if (field == "leg3_level2")
                    o.leg3_level2 = std::stoi(value);
                else if (field == "leg3_actual_fill_qty")
                    o.leg3_actual_fill_qty = std::stoul(value);

                // leg4 details
                else if (field == "leg4_filled")
                    o.leg4_filled = std::stoi(value);
                else if (field == "leg4_filled_qty")
                    o.leg4_filled_qty = std::stoul(value);
                else if (field == "leg4_counter")
                    o.leg4_counter = std::stoul(value);
                else if (field == "leg4_depth")
                    o.leg4_depth = std::stoul(value);
                else if (field == "leg4_price")
                    o.leg4_price = std::stoul(value);
                else if (field == "last_leg4_price")
                    o.last_leg4_price = std::stoul(value);
                else if (field == "last_leg4_qty")
                    o.last_leg4_qty = std::stoul(value);
                else if (field == "last_covered_leg4")
                    o.last_covered_leg4 = std::stoul(value);
                else if (field == "legs4_timer_start")
                    o.legs4_timer_start = std::stoull(value);
                else if (field == "legs4_level")
                    o.legs4_level = std::stoul(value);
                else if (field == "leg4_level2")
                    o.leg4_level2 = std::stoi(value);
                else if (field == "leg4_actual_fill_qty")
                    o.leg4_actual_fill_qty = std::stoul(value);

                // pnl
                else if (field == "v1")
                    o.v1 = std::stoul(value);
                else if (field == "q1")
                    o.q1 = std::stoul(value);
                else if (field == "v2")
                    o.v2 = std::stoul(value);
                else if (field == "q2")
                    o.q2 = std::stoul(value);
                else if (field == "v3")
                    o.v3 = std::stoul(value);
                else if (field == "q3")
                    o.q3 = std::stoul(value);
                else if (field == "v4")
                    o.v4 = std::stoul(value);
                else if (field == "q4")
                    o.q4 = std::stoul(value);

                // calculated
                else if (field == "price_leg1_for_entire_cycle")
                    o.price_leg1_for_entire_cycle = std::stoul(value);
                else if (field == "price_leg2_for_entire_cycle")
                    o.price_leg2_for_entire_cycle = std::stoul(value);
                else if (field == "price_leg3_for_entire_cycle")
                    o.price_leg3_for_entire_cycle = std::stoul(value);
                else if (field == "price_leg4_for_entire_cycle")
                    o.price_leg4_for_entire_cycle = std::stoul(value);
                else if (field == "strike_diff")
                    o.strike_diff = std::stoul(value);
                else if (field == "achieved_spread")
                    o.achieved_spread = std::stoll(value);

                // legacy
                else if (field == "bid_traded_qty")
                    o.bid_traded_qty = std::stoul(value);
                else if (field == "bid_ordered_qty")
                    o.bid_ordered_qty = std::stoul(value);
                else if (field == "new_max")
                    o.new_max = std::stoul(value);
                else if (field == "mod_max")
                    o.mod_max = std::stoul(value);

                // rejections
                else if (field == "modify_reject")
                    o.modify_reject = std::stoi(value);
                else if (field == "cancel_reject")
                    o.cancel_reject = std::stoi(value);
            }

            portfolios[pf_id].order_data.box_bidding = o;
            std::cout << "Recovered BoxBidding Order for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_boxioc_params(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {
        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        redisFree(ctx);
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS boxioc_params_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        // Member is the numeric portfolio/strategy id
        uint16_t pf_id = static_cast<uint16_t>(std::stoi(reply->element[i]->str));
        std::string key = "boxioc_params:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());
        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            BoxIocParams p{}; // default-initialized

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "call_itm_token")
                    p.call_itm_token = static_cast<uint32_t>(std::stoul(value));
                else if (field == "put_otm_token")
                    p.put_otm_token = static_cast<uint32_t>(std::stoul(value));
                else if (field == "call_otm_token")
                    p.call_otm_token = static_cast<uint32_t>(std::stoul(value));
                else if (field == "put_itm_token")
                    p.put_itm_token = static_cast<uint32_t>(std::stoul(value));

                else if (field == "price_difference")
                    p.price_difference = static_cast<int64_t>(std::stoll(value));
                else if (field == "is_flip_box")
                    p.is_flip_box = (std::stoi(value) != 0);

                else if (field == "max_lots")
                    p.max_lots = static_cast<uint32_t>(std::stoul(value));
                else if (field == "sol")
                    p.sol = static_cast<uint32_t>(std::stoul(value));

                else if (field == "timer_ms")
                    p.timer_ms = static_cast<uint32_t>(std::stoul(value));
            }

            // Assign back to portfolio (adjust path to your struct)
            portfolios[pf_id].params.box_ioc = p;
            std::cout << "Recovered BoxIOC Params for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_boxioc_order_data(const char *host, int port, Portfolio (&portfolios)[MAX_PORTFOLIOS], int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {
        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        redisFree(ctx);
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS boxioc_orders_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint16_t pf_id = static_cast<uint16_t>(std::stoi(reply->element[i]->str));
        std::string key = "boxioc_order:" + std::to_string(pf_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());
        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            BoxIocOrderData d{}; // default-initialized

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                // Core state & flags
                if (field == "state")
                    d.state = static_cast<BoxIocStrategyState>(std::stoi(value));
                else if (field == "is_flip_box")
                    d.is_flip_box = (std::stoi(value) != 0);

                // ITM legs
                else if (field == "call_itm_order_id")
                    d.call_itm_order_id = static_cast<uint32_t>(std::stoul(value));
                else if (field == "put_itm_order_id")
                    d.put_itm_order_id = static_cast<uint32_t>(std::stoul(value));
                else if (field == "call_itm_pending_qty")
                    d.call_itm_pending_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "put_itm_pending_qty")
                    d.put_itm_pending_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "call_itm_filled_qty")
                    d.call_itm_filled_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "put_itm_filled_qty")
                    d.put_itm_filled_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "call_itm_ack")
                    d.call_itm_ack = (std::stoi(value) != 0);
                else if (field == "put_itm_ack")
                    d.put_itm_ack = (std::stoi(value) != 0);
                else if (field == "call_itm_cancelled")
                    d.call_itm_cancelled = (std::stoi(value) != 0);
                else if (field == "put_itm_cancelled")
                    d.put_itm_cancelled = (std::stoi(value) != 0);
                else if (field == "call_itm_filled")
                    d.call_itm_filled = (std::stoi(value) != 0);
                else if (field == "put_itm_filled")
                    d.put_itm_filled = (std::stoi(value) != 0);

                // OTM second leg (PUT)
                else if (field == "second_leg_order_id")
                    d.second_leg_order_id = static_cast<uint32_t>(std::stoul(value));
                else if (field == "second_leg_pending_qty")
                    d.second_leg_pending_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "second_leg_filled_qty")
                    d.second_leg_filled_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "second_leg_price")
                    d.second_leg_price = static_cast<uint32_t>(std::stoul(value));
                else if (field == "second_leg_depth")
                    d.second_leg_depth = static_cast<uint32_t>(std::stoul(value));
                else if (field == "second_leg_counter")
                    d.second_leg_counter = static_cast<uint32_t>(std::stoul(value));
                else if (field == "second_leg_ack")
                    d.second_leg_ack = (std::stoi(value) != 0);
                else if (field == "second_leg_timer_identifier")
                    d.second_leg_timer_identifier = (std::stoi(value) != 0);
                else if (field == "last_quantity_second_leg")
                    d.last_quantity_second_leg = static_cast<uint32_t>(std::stoul(value));

                // OTM third leg (CALL)
                else if (field == "third_leg_order_id")
                    d.third_leg_order_id = static_cast<uint32_t>(std::stoul(value));
                else if (field == "third_leg_pending_qty")
                    d.third_leg_pending_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "third_leg_filled_qty")
                    d.third_leg_filled_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "third_leg_price")
                    d.third_leg_price = static_cast<uint32_t>(std::stoul(value));
                else if (field == "third_leg_depth")
                    d.third_leg_depth = static_cast<uint32_t>(std::stoul(value));
                else if (field == "third_leg_counter")
                    d.third_leg_counter = static_cast<uint32_t>(std::stoul(value));
                else if (field == "third_leg_ack")
                    d.third_leg_ack = (std::stoi(value) != 0);
                else if (field == "third_leg_timer_identifier")
                    d.third_leg_timer_identifier = (std::stoi(value) != 0);
                else if (field == "last_quantity_third_leg")
                    d.last_quantity_third_leg = static_cast<uint32_t>(std::stoul(value));

                // Timers
                else if (field == "itm_timer_start")
                    d.itm_timer_start = static_cast<uint64_t>(std::stoull(value));
                else if (field == "otm_timer_start")
                    d.otm_timer_start = static_cast<uint64_t>(std::stoull(value));
                else if (field == "second_otm_timer_start")
                    d.second_otm_timer_start = static_cast<uint64_t>(std::stoull(value));
                else if (field == "third_otm_timer_start")
                    d.third_otm_timer_start = static_cast<uint64_t>(std::stoull(value));
                else if (field == "timer_value")
                    d.timer_value = static_cast<uint32_t>(std::stoul(value));

                // Spread & quantities
                else if (field == "strike_difference")
                    d.strike_difference = static_cast<int64_t>(std::stoll(value));
                else if (field == "current_spread")
                    d.current_spread = static_cast<int64_t>(std::stoll(value));
                else if (field == "traded_qty")
                    d.traded_qty = static_cast<uint32_t>(std::stoul(value));

                // Global flags
                else if (field == "first_leg_trade")
                    d.first_leg_trade = (std::stoi(value) != 0);
                else if (field == "second_leg_trade")
                    d.second_leg_trade = (std::stoi(value) != 0);
                else if (field == "place_second_order")
                    d.place_second_order = (std::stoi(value) != 0);
                else if (field == "place_aggressive")
                    d.place_aggressive = (std::stoi(value) != 0);
                else if (field == "modify_flag")
                    d.modify_flag = (std::stoi(value) != 0);
                else if (field == "terminate_check")
                    d.terminate_check = (std::stoi(value) != 0);
            }

            // Assign back to portfolio (adjust path to your struct)
            portfolios[pf_id].order_data.box_ioc = d;
            std::cout << "Recovered BoxIOC Order for Portfolio: " << pf_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_strategy_leg_data(const char *host, int port,
                                      ska::flat_hash_map<uint32_t, StrategyLegData> &oms_to_leg, int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Connection failed" << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {

        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        return;
    }

    redisReply *reply = (redisReply *)redisCommand(ctx, "SMEMBERS strategy_legs_list");
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply)
            freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    for (size_t i = 0; i < reply->elements; i++)
    {
        uint32_t oms_id = std::stoul(reply->element[i]->str);
        std::string key = "strategy_leg:" + std::to_string(oms_id);

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", key.c_str());

        if (hgetall && hgetall->type == REDIS_REPLY_ARRAY)
        {
            StrategyLegData s{};
            s.oms_order_id = oms_id;

            for (size_t j = 0; j < hgetall->elements; j += 2)
            {
                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "token")
                    s.token = std::stoul(value);
                else if (field == "side")
                    s.side = static_cast<OrderSide>(std::stoi(value));
                else if (field == "portfolio_id")
                    s.portfolio_id = std::stoi(value);
                else if (field == "fill_price_sum")
                    s.fill_price_sum = std::stoul(value);
                else if (field == "fill_qty_sum")
                    s.fill_qty_sum = std::stoul(value);
                else if (field == "required_qty")
                    s.required_qty = std::stoul(value);
                else if (field == "exchange_order_id")
                    s.exchange_order_id = std::stoul(value);
                else if (field == "exchange_modified_time")
                    s.exchange_modified_time = std::stoull(value);
                else if (field == "order_state")
                    s.order_state = static_cast<OrderState>(std::stoi(value));
            }

            oms_to_leg[oms_id] = s;
            std::cout << "Recovered Strategy Leg: " << oms_id << "\n";
        }

        if (hgetall)
            freeReplyObject(hgetall);
    }

    freeReplyObject(reply);
    redisFree(ctx);
}

inline void recover_trades(const char *host, int port,
                           std::vector<Trade> &trades, int redis_db_index)
{
    redisContext *ctx = redisConnect(host, port);
    if (ctx == NULL || ctx->err)
    {
        std::cerr << "Redis connection failed: "
                  << (ctx ? ctx->errstr : "null context") << std::endl;
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!check_db(ctx, redis_db_index))
    {
        std::cout << "Not able to select index 1 or name mismatch" << std::endl;
        redisFree(ctx);
        return;
    }

    // Check if trades sorted set exists
    redisReply *exists_reply = (redisReply *)redisCommand(ctx, "EXISTS trades");
    if (exists_reply && exists_reply->type == REDIS_REPLY_INTEGER)
    {
        if (exists_reply->integer == 0)
        {
            std::cout << "Trades sorted set does not exist" << std::endl;
            freeReplyObject(exists_reply);
            redisFree(ctx);
            return;
        }
        freeReplyObject(exists_reply);
    }

    // FIXED: Use ZRANGE for sorted set (trades is a ZSET, not a SET)
    // Get all trade keys from sorted set ordered by timestamp
    redisReply *reply = (redisReply *)redisCommand(ctx, "ZRANGE trades 0 -1");

    if (!reply)
    {
        std::cerr << "Failed to get trades list: " << ctx->errstr << std::endl;
        redisFree(ctx);
        return;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr << "Redis error: " << reply->str << std::endl;
        freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    if (reply->type != REDIS_REPLY_ARRAY)
    {
        std::cerr << "Unexpected reply type: " << reply->type
                  << " (expected REDIS_REPLY_ARRAY=" << REDIS_REPLY_ARRAY << ")" << std::endl;
        freeReplyObject(reply);
        redisFree(ctx);
        return;
    }

    std::cout << "Found " << reply->elements << " trade keys in sorted set\n";
    trades.reserve(reply->elements);

    for (size_t i = 0; i < reply->elements; i++)
    {
        if (!reply->element[i] || reply->element[i]->type != REDIS_REPLY_STRING)
        {
            std::cerr << "Invalid element at index " << i << std::endl;
            continue;
        }

        // ZRANGE returns the full key already (e.g., "trade:1234567890:42")
        // Use it directly - do NOT prepend "trade:" again
        std::string trade_key = reply->element[i]->str;

        std::cout << "Fetching: " << trade_key << std::endl;

        redisReply *hgetall = (redisReply *)redisCommand(ctx, "HGETALL %s", trade_key.c_str());

        if (!hgetall)
        {
            std::cerr << "Failed to fetch " << trade_key << ": " << ctx->errstr << std::endl;
            continue;
        }

        if (hgetall->type == REDIS_REPLY_ERROR)
        {
            std::cerr << "Redis error for " << trade_key << ": " << hgetall->str << std::endl;
            freeReplyObject(hgetall);
            continue;
        }

        if (hgetall->type == REDIS_REPLY_ARRAY && hgetall->elements > 0)
        {
            Trade t{};

            // Parse hash fields
            for (size_t j = 0; j + 1 < hgetall->elements; j += 2)
            {
                if (!hgetall->element[j] || !hgetall->element[j + 1])
                    continue;

                std::string field = hgetall->element[j]->str;
                std::string value = hgetall->element[j + 1]->str;

                if (field == "oms_order_id")
                    t.oms_order_id = static_cast<uint32_t>(std::stoul(value));
                else if (field == "portfolio_id")
                    t.portfolio_id = static_cast<uint32_t>(std::stoul(value));
                else if (field == "fill_qty")
                    t.fill_qty = static_cast<uint32_t>(std::stoul(value));
                else if (field == "fill_price")
                    t.fill_price = static_cast<uint32_t>(std::stoul(value));
                else if (field == "exchange_order_id")
                    t.exchange_order_id = static_cast<uint64_t>(std::stoull(value));
                else if (field == "timestamp")
                    t.timestamp = static_cast<uint64_t>(std::stoull(value));
                else if (field == "partial_fill")
                    t.partial_fill = std::stoi(value);
            }

            trades.push_back(t);
            std::cout << "Recovered Trade: oms_order_id=" << t.oms_order_id
                      << ", fill_qty=" << t.fill_qty
                      << ", timestamp=" << t.timestamp << "\n";
        }
        else
        {
            std::cout << "Empty or invalid hash for " << trade_key
                      << " (elements: " << hgetall->elements << ")" << std::endl;
        }

        freeReplyObject(hgetall);
    }

    std::cout << "Total recovered trades: " << trades.size() << "\n";

    freeReplyObject(reply);
    redisFree(ctx);
}

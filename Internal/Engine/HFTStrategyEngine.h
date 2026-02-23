// HFTStrategyEngine.h
#pragma once

#include "Network/SocketManager.h"

#include "Include/Types.h"

#include "Utils/MemoryPool.h"
#include "Utils/RingBuffer.h"
#include "Utils/shm_logger.hpp"
#include "Utils/db_redis.h"
#include "Utils/StrategyId.h"
#include "Utils/log_shm.hpp"
#include "Utils/tcp_ring_buffer.hpp"

#include "../sdk/strategy_sdk.h"
#include "OrderManagement/OrderManager.h"

// #include "Strategies/TemplateStrategy.h"
// #include "Strategies/TemplateHelper.h"
// #include "Strategies/Bidding3Template.h"
// #include "Strategies/ConRevTemplate.h"

#include "Library/flat_hash_map.hpp"

#include <vector>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <immintrin.h>
#include <sys/epoll.h>
#include <unordered_set>
#include <climits>
#include <atomic>
#include <string>
#include <iostream>
#include <array>
#include <dlfcn.h>  // For dlopen, dlsym, dlclose

struct PlatformContext;

// Forward declare C ABI functions with extern "C" linkage
extern "C" {
    void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token);
    void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token);
}


class HFTStrategyEngine
{
public:
    HFTStrategyEngine(const Config &cfg);
    ~HFTStrategyEngine();
    bool initialize();
    void run();

    // Quick Function to Push to Reddis Queue
    template <typename Func>
    ALWAYS_INLINE bool try_push_with_retry(DBQueue *queue, Func &&construct_fn, int max_retries = 3) noexcept
    {
        for (int i = 0; i < max_retries; ++i)
        {
            size_t h = queue->head.load(std::memory_order_relaxed);
            size_t next = (h + 1) & (DB_QUEUE_SIZE - 1);

            if (next == queue->cached_tail)
            {
                queue->cached_tail = queue->tail.load(std::memory_order_acquire);
                if (next == queue->cached_tail)
                {
                    _mm_pause();
                    continue;
                }
            }

            // Construct directly in buffer
            construct_fn(queue->buffer[h]);
            queue->head.store(next, std::memory_order_release);
            return true;
        }
        return false;
    }

    ALWAYS_INLINE void push_oms_data(StrategyLegData &leg_data) noexcept
    {
        try_push_with_retry(redis_db_shm_q, [&](ShmMessage &msg)
                            {
            msg.type = ShmMessageType::StrategyLegData;
            msg.oms = leg_data; });
    }

    ALWAYS_INLINE void push_portfolio(Portfolio &portfolio) noexcept
    {
        try_push_with_retry(redis_db_shm_q, [&](ShmMessage &msg)
                            {
            msg.type = ShmMessageType::Portfolio;
            msg.portfolio.portfolio_id    = portfolio.portfolio_id;
            msg.portfolio.kind            = portfolio.kind;
            msg.portfolio.leg_count       = portfolio.leg_count;
            msg.portfolio.is_active       = portfolio.is_active;
            msg.portfolio.terminate       = portfolio.terminate;
            msg.portfolio.stale_status    = portfolio.stale_status;
            msg.portfolio.is_iter_over    = portfolio.is_iter_over;
            msg.portfolio.stop_requested  = portfolio.stop_requested;
            msg.portfolio.traded_qty      = portfolio.traded_qty;
            msg.portfolio.achieved_spread = portfolio.achieved_spread;
            
            // Use memcpy for array copy - faster than loop
            std::memcpy(msg.portfolio.legs, portfolio.legs, sizeof(portfolio.legs[0]) * 4); });
    }

    ALWAYS_INLINE void push_params(Portfolio &portfolio) noexcept
    {
        try_push_with_retry(redis_db_shm_q, [&](ShmMessage &msg)
                            {
            switch (portfolio.kind) {
                case StrategyKind::CONREV_IOC:
                    msg.type = ShmMessageType::ThreeLegIOCParams;
                    msg.three_leg_ioc_params = portfolio.params.conrev;
                    break;
                case StrategyKind::CONREV_BID:
                    msg.type = ShmMessageType::ThreeLegBiddingParams;
                    msg.three_leg_bidding_params = portfolio.params.three_leg_bidding;
                    break;
                case StrategyKind::BOX_1_1_1_1:
                    msg.type = ShmMessageType::BoxBiddingParams;
                    msg.box_bidding_params = portfolio.params.box_bidding;
                    break;
                case StrategyKind::BOX_2_1_1:
                    msg.type = ShmMessageType::BoxIOCParams;
                    msg.box_ioc_params = portfolio.params.box_ioc;
                    break;
                
                default:
                    break;
            } });
    }

    ALWAYS_INLINE void push_order_data(Portfolio &portfolio) noexcept
    {
        try_push_with_retry(redis_db_shm_q, [&](ShmMessage &msg)
                            {
            switch (portfolio.kind) {
                case StrategyKind::CONREV_IOC:
                    msg.type = ShmMessageType::ThreeLegIOCOrderData;
                    msg.three_leg_ioc_data = portfolio.order_data.conrev;
                    break;
                case StrategyKind::CONREV_BID:
                    msg.type = ShmMessageType::ThreeLegBiddingOrderData;
                    msg.three_leg_bidding_data = portfolio.order_data.three_leg_bidding;
                    break;
                case StrategyKind::BOX_1_1_1_1:
                    msg.type = ShmMessageType::BoxBiddingOrderData;
                    msg.box_bidding_data = portfolio.order_data.box_bidding;
                    break;
                case StrategyKind::BOX_2_1_1:
                    msg.type = ShmMessageType::BoxIOCOrderData;
                    msg.box_ioc_data = portfolio.order_data.box_ioc;
                    break;
                default:
                    break;
            } });
    }

    ALWAYS_INLINE void push_trades(uint32_t oms_order_id,
                                   uint32_t fill_qty,
                                   uint32_t fill_price,
                                   uint64_t exchange_order_id,
                                   uint64_t timestamp,
                                   bool partial_fill) noexcept
    {
        try_push_with_retry(redis_db_shm_q, [&](ShmMessage &msg)
                            {
            msg.type = ShmMessageType::Trade;
            msg.trade.exchange_order_id = exchange_order_id;
            msg.trade.fill_price = fill_price;
            msg.trade.fill_qty = fill_qty;
            msg.trade.timestamp = timestamp;
            msg.trade.partial_fill = partial_fill;
            msg.trade.oms_order_id = oms_order_id; });
    }

    ALWAYS_INLINE void push_logs(bool is_utrade,uint8_t msg_type) noexcept
    {
        try_push_with_retry(redis_db_shm_q, [&](ShmMessage &msg)
                            {
            msg.type = ShmMessageType::LogMessage;
            msg.log_shm.is_utrade = is_utrade;
            msg.log_shm.msg_type = msg_type;
            });
    }

    friend void platform_subscribe_token(PlatformContext*, uint32_t, uint32_t);
    friend void platform_unsubscribe_token(PlatformContext*, uint32_t, uint32_t);
    
private:
    std::string module = "HFTStrategyEngine";
    Config config;
    SocketManager socketManager;

    // alignas(64) Portfolio portfolios[MAX_PORTFOLIOS];
    // alignas(64) bool portfolio_initialized[MAX_PORTFOLIOS];

    ska::flat_hash_map<uint32_t, StrategyLegData> oms_to_leg;
    ska::flat_hash_map<uint16_t, std::vector<uint32_t>> open_orders;
    ska::flat_hash_map<uint32_t, StrategyLegData> old_oms_to_leg;

    ska::flat_hash_map<uint32_t, ska::flat_hash_set<uint16_t>> token_to_portfolios;
    ska::flat_hash_map<uint32_t, StoredMarketDataLatency> orderbook;
    ska::flat_hash_map<uint8_t, StreamData> stream_to_sequence;
    std::unique_ptr<OrderManager> order_manager;

    bool market_data_not_coming = false;
    int market_data_threshold = 50;
    TimePoint last_market_event_time = Clock::now();
    int writing_to_file = 0;

    TCPReceiveBuffer<OrderMessage, 1 << 20> utrade_rx_buffer;      // 256KB for UTrade 1 << 20 ,524288
    // TCPReceiveBuffer<FrontendMessage, 1 << 20> frontend_rx_buffer; // 256KB for Frontend
    TCPReceiveBuffer<FrontendCommand, 1 << 20> frontend_rx_buffer; // 256KB for Frontend

    ShmLogger trade_logger;
    DBQueue *redis_db_shm_q;
    bool redis_db_recovered = false;
    const char *redis_host;
    int redis_port;

    void handle_frontend_event();
    // void recovery_redis_db();
    std::string get_nse_fo_contract_name();
 
    /* ════════════════════════════════════════════════════════
     * NEW: .so PLUGIN MANAGEMENT
     * ════════════════════════════════════════════════════════ */
    
    struct StrategyPlugin {
        void*    dl_handle;
        uint32_t type_id;
        char     so_path[256];
        
        // Function pointers from .so
        uint32_t (*get_type_id)(void);
        void*    (*create)(StrategyFnTable*);
        void     (*destroy_all)(void);
    };
    
    struct PortfolioSlot {
        StrategyPlugin*   plugin;
        void*             strategy_handle;  // Opaque pointer from .so
        StrategyFnTable   fn_table;         // Callbacks
        bool              allocated;
    };
    
    StrategyPlugin  plugins[32];
    uint8_t         plugin_count;
    PortfolioSlot   portfolio_slots[MAX_PORTFOLIOS];
    
    // Platform API for strategies
    static PlatformAPI s_platform_api;
    
    // Helper methods
    void setup_platform_api();
    StrategyPlugin* find_plugin_by_type(uint32_t type_id);
    bool load_strategy_plugin(const char* so_path);
    void unload_strategy_plugin(uint32_t type_id);
    
    // Platform API implementations (static for C ABI)
                        
    static int32_t api_place_new_order_multi_leg(PlatformContext* ctx, uint32_t pf_id,
                                       Leg* legs, uint8_t leg_count, OrderType order_type, unsigned long long event_time);
  
    static int32_t api_place_modify_order(PlatformContext* ctx, uint32_t pf_id,
                                          uint32_t oms_order_id,
                                          int64_t new_price, int32_t new_qty);
    static int32_t api_place_cancel_order(PlatformContext* ctx, uint32_t pf_id,
                                          uint32_t oms_order_id);
    static int32_t api_get_position(PlatformContext* ctx, uint32_t pf_id,
                                    uint32_t token, PositionView* out);
    static int32_t api_get_open_orders(PlatformContext* ctx, uint32_t pf_id,
                                       OpenOrderView* out_buf, int32_t max);
    static void api_log_msg(PlatformContext* ctx, uint32_t pf_id,
                           const char* msg, uint32_t len);
    static void api_send_status_update(PlatformContext *ctx, uint32_t pf_id,
                            const StrategyStatusUpdate *update);
};
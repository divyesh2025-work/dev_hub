// OrderManager.h
#pragma once
#include <iostream>
#include <atomic>
#include <sys/socket.h>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <unistd.h>

#include <Include/orders/OrderEnums.h>
#include <Include/orders/OrderStructs.h>
#include <Utils/Helper.h>
#include <Utils/MemoryPool.h>
#include <Utils/LatencyMonitor.h>
#include <Utils/StrategyId.h>
#include <Utils/rate_limiter.h>
#include <Library/flat_hash_map.hpp>
class OrderManager
{
public:
    OrderManager(int socket_fd, size_t order_capacity = 90, double window_seconds = 1.0);

    int getSocket() const { return oms_socket; }

    bool sendSingleLegOrder(uint16_t portfolio_id,
                            uint32_t &leg_order_id,
                            OrderType order_type,
                            Leg &leg, const unsigned long long exe_time, bool is_bid_leg);

    bool sendTwoLegOrder(uint16_t portfolio_id,
                         uint32_t &leg1_order_id,
                         uint32_t &leg2_order_id,
                         OrderType order_type,
                         Leg &leg1,
                         Leg &leg2, const unsigned long long exe_time, bool is_bid_leg);

    bool sendThreeLegOrder(uint16_t portfolio_id,
                           uint32_t &leg1_order_id,
                           uint32_t &leg2_order_id,
                           uint32_t &leg3_order_id,
                           OrderType order_type,
                           Leg &leg1,
                           Leg &leg2,
                           Leg &leg3, const unsigned long long exe_time, bool is_bid_leg);

    bool sendOrderPlacement(uint16_t portfolio_id,
                            OrderType order_type,
                            const Leg *legs,
                            uint8_t num_legs, const unsigned long long exe_time, bool is_bid_leg);

    bool sendModifyPlacement(uint16_t portfolio_id, uint32_t strategy_order_id,
                             const Leg leg, const unsigned long long exe_time, int leg_num = 0);

    bool sendCancelPlacement(uint16_t portfolio_id, uint32_t strategy_order_id);
    void reconfigureRateLimiter(size_t new_capacity, double new_window_sec);

    template <size_t N>
    ALWAYS_INLINE bool sendMultiLegOrder(
        uint16_t portfolio_id,
        OrderType order_type,
        Leg *legs,
        const unsigned long long exe_time,
        bool is_bid_leg) noexcept;

    static LatencyMonitor latency;

    StrategyOrderIDManager& strategy_order_id_manager;

    ALWAYS_INLINE uint32_t next_strategy_order_id() noexcept {
        return strategy_order_id_manager.generate();
    }

private:
    int oms_socket;
    OrderRateLimiter rate_limiter;

    std::atomic<uint64_t> order_id_counter{1};

    std::string module = "OrderManager";

    

    uint64_t get_timestamp_ns();
};
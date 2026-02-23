// OrderManager.cpp
#include "OrderManager.h"
LatencyMonitor OrderManager::latency;
OrderManager::OrderManager(int socket_fd, size_t order_capacity, double window_seconds)
    : oms_socket(socket_fd), rate_limiter(order_capacity, window_seconds)
{
}

bool OrderManager::sendOrderPlacement(uint16_t portfolio_id,
                                      OrderType order_type,
                                      const Leg *legs,
                                      uint8_t num_legs, const unsigned long long exe_time, bool is_bid_leg)
{
    if (!legs || num_legs == 0) [[unlikely]]
    {
        std::cerr<<"legs not valid or num legs 0::" << num_legs<<std::endl;
        LOG_LIVE(module, "legs not valid or num legs 0");

        return false;
    }

    // Use RDTSC for ultra-low latency timing (~3-5 CPU cycles)
    uint64_t current_cycles = rate_limiter.getRdtscp();
    // SINGLE BRANCH: Check rate limit for bid orders
    // Cover orders skip this (is_bid_leg == false means short-circuit)
    if (is_bid_leg && !rate_limiter.canSendAndRecord(current_cycles)) [[unlikely]]
    {
        LOG_FILE(module, "[BLOCKED] Bid order - 90/sec limit");
        LOG_LIVE(module, "[BLOCKED] Bid order - 90/sec limit");
        std::cerr<<"[BLOCKED] Bid order - 90/sec limit" <<std::endl;
        return false;
    }

    OrderMessage msg{};
    memset(&msg, 0, sizeof(msg));
    msg.type = OrderMessageType::OrderPlacement;
    msg.new_order.num_legs = num_legs;
    msg.new_order.type = order_type;
    msg.new_order.portfolio_id = portfolio_id;

    for (uint8_t i = 0; i < msg.new_order.num_legs; ++i)
    {
        msg.new_order.legs[i] = legs[i];
        // LOG_COUT("Legs information that coming:: id:" << legs[i].symbol_id << ", price: " << legs[i].price << ", Qty:" << legs[i].qty);
        LOG_FILE(module, "Legs information that coming:: id:" + std::to_string(legs[i].symbol_id) + ", price: " + std::to_string(legs[i].price) + ", Qty:" + std::to_string(legs[i].qty));
        // LOG_COUT("Legs information that sending:: id:" << msg.new_order.legs[i].symbol_id << ", price: " << msg.new_order.legs[i].price << ", Qty:" << msg.new_order.legs[i].qty);
        LOG_FILE(module, "Legs information that sending:: id:" + std::to_string(msg.new_order.legs[i].symbol_id) + ", price: " + std::to_string(msg.new_order.legs[i].price) + ", Qty:" + std::to_string(msg.new_order.legs[i].qty));
        LOG_FILE(module, "OMS order id:" + std::to_string(msg.new_order.legs[i].oms_order_id));
    }

    // LOG_COUT("Number of legs:" << static_cast<int>(msg.new_order.num_legs));

    latency.end_monitor1(exe_time);

    const ssize_t sent = ::send(oms_socket, &msg, sizeof(msg), MSG_DONTWAIT);

    if (__builtin_expect(sent == -1, 0)) [[unlikely]]
    {
        int err = errno;
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf),
                 "[ERROR] send() failed (errno=%d: %s)", err, strerror(err));
        LOG_FILE(module, log_buf);
        return false;
    }

    // LOG_COUT("Order sent successfully, SIZE:" << sizeof(msg));

    return true;
}

bool OrderManager::sendModifyPlacement(uint16_t portfolio_id, uint32_t oms_order_id,

                                       const Leg leg, const unsigned long long exe_time, int leg_num)
{
    OrderMessage msg{};
    memset(&msg, 0, sizeof(msg));
    msg.type = OrderMessageType::ModifyRequest;

    msg.modify.oms_order_id = oms_order_id;
    // LOG_COUT("Sending Modify for  oms id:" << msg.modify.oms_order_id);

    msg.modify.new_leg = leg;
    msg.modify.new_leg.oms_order_id = oms_order_id;
    LOG_FILE(module, "Modifying  order for  oms id is:" + std::to_string(msg.modify.oms_order_id));
    LOG_FILE(module, "Leg info of modfiy order:(qty,price)" + std::to_string(leg.qty) + "," + std::to_string(leg.price));
    latency.end_monitor1(exe_time);
    const ssize_t sent = ::send(oms_socket, &msg, sizeof(msg), MSG_DONTWAIT);

    return sent == sizeof(msg);
}

bool OrderManager::sendCancelPlacement(uint16_t portfolio_id, uint32_t oms_order_id)
{

    OrderMessage msg{};
    memset(&msg, 0, sizeof(msg));
    msg.type = OrderMessageType::CancelRequest;
    msg.cancel.oms_order_id = oms_order_id;
    LOG_FILE(module, "Sending Cancel for  oms id:" + std::to_string(msg.modify.oms_order_id));
    // LOG_COUT(module << ",Sending Cancel for  oms id:" + std::to_string(msg.modify.oms_order_id));

    const ssize_t sent = ::send(oms_socket, &msg, sizeof(msg), MSG_DONTWAIT);

    return sent == sizeof(msg);
}

bool OrderManager::sendSingleLegOrder(uint16_t portfolio_id,
                                      uint32_t &leg_order_id,
                                      OrderType order_type,
                                      Leg &leg, const unsigned long long exe_time, bool is_bid_leg)
{
    int32_t strategy_order_id = next_strategy_order_id();
    leg_order_id = strategy_order_id;
    leg.oms_order_id = leg_order_id;

    LOG_FILE(module, "leg.oms_order_id " + std::to_string(leg.oms_order_id));
    LOG_FILE(module, "Leg info of sending order:(qty,price)" + std::to_string(leg.qty) + "," + std::to_string(leg.price));
    return sendOrderPlacement(portfolio_id, order_type, &leg, 1, exe_time, is_bid_leg);
}

bool OrderManager::sendTwoLegOrder(uint16_t portfolio_id,
                                   uint32_t &leg1_order_id,
                                   uint32_t &leg2_order_id,
                                   OrderType order_type,
                                   Leg &leg1,
                                   Leg &leg2, const unsigned long long exe_time, bool is_bid_leg)
{
    uint32_t strategy_order_id_1 = next_strategy_order_id();
    leg1_order_id = strategy_order_id_1;
    uint32_t strategy_order_id_2 = next_strategy_order_id();
    leg2_order_id = strategy_order_id_2;
    leg1.oms_order_id = strategy_order_id_1;
    leg2.oms_order_id = strategy_order_id_2;
    Leg legs[2] = {leg1, leg2};
    return sendOrderPlacement(portfolio_id, order_type, legs, 2, exe_time, is_bid_leg);
}

bool OrderManager::sendThreeLegOrder(uint16_t portfolio_id,
                                     uint32_t &leg1_order_id,
                                     uint32_t &leg2_order_id,
                                     uint32_t &leg3_order_id,
                                     OrderType order_type,
                                     Leg &leg1,
                                     Leg &leg2,
                                     Leg &leg3, const unsigned long long exe_time, bool is_bid_leg)
{
    uint32_t strategy_order_id = next_strategy_order_id();
    leg1_order_id = strategy_order_id;
    strategy_order_id = next_strategy_order_id();
    leg2_order_id = strategy_order_id;
    strategy_order_id = next_strategy_order_id();
    leg3_order_id = strategy_order_id;
    leg1.oms_order_id = leg1_order_id;
    leg2.oms_order_id = leg2_order_id;
    leg3.oms_order_id = leg3_order_id;
    Leg legs[3] = {leg1, leg2, leg3};
    // LOG_COUT(" Printing price of each leg:" << leg1.price << "," << leg2.price << "," << leg3.price);

    return sendOrderPlacement(portfolio_id, order_type, legs, 3, exe_time, is_bid_leg);
}

template<size_t N>
ALWAYS_INLINE bool sendMultiLegOrder(
    uint16_t portfolio_id,
    OrderType order_type,
    Leg* legs,
    const unsigned long long exe_time,
    bool is_bid_leg) noexcept
{
    auto& id_manager = id_gen;   // stored reference inside class

    // // Batch ID allocation (single atomic if possible)
    // uint32_t base_id = id_manager.generate_batch<N>();

    // #pragma unroll
    // for (size_t i = 0; i < N; ++i)
    // {
    //     legs[i].oms_order_id = base_id + i;
    // }

    #pragma unroll
    for (size_t i = 0; i < N; ++i)
        legs[i].oms_order_id = next_strategy_order_id();

    return sendOrderPlacement(
        portfolio_id,
        order_type,
        legs,
        N,
        exe_time,
        is_bid_leg
    );
}


void OrderManager::reconfigureRateLimiter(size_t new_capacity, double new_window_sec)
{
    rate_limiter.reconfigure(new_capacity, new_window_sec);

    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf),
             "[CONFIG] Rate limiter updated: %zu orders per %.2f seconds",
             new_capacity, new_window_sec);
    LOG_FILE(module, log_buf);
}
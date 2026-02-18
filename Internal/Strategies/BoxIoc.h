#pragma once
#include "TemplateStrategy.h"
#include <chrono>
#include <unordered_map>

// Box Strategy Template Implementation
template <>
struct StrategyExecutor<StrategyKind::BOX_2_1_1>
{
    static bool run(Portfolio &p,
                    const StrategyMarketSnapshot &s,
                    OrderManager &o,
                    const unsigned long long exe_time) noexcept
    {
        auto &params = p.params.box_ioc;
        auto &order_data = p.order_data.box_ioc;

        LOG_FILE("BOX_STRATEGY", "Starting with state:" + std::to_string(static_cast<int>(order_data.state)));
        LOG_FILE("BOX_STRATEGY", "Traded qty:" + std::to_string(order_data.traded_qty) +
                                     " max lots:" + std::to_string(params.max_lots));

        // Fast early termination - check if max lots reached
        if (__builtin_expect(order_data.traded_qty >= params.max_lots, 0))
        {
            p.is_active = 0;
            p.terminate = 1;
            return false;
        }

        // Get current timestamp
        const uint64_t current_time = getCurrentTimestamp();

        auto &box_market_snapshot = s.data.box_ioc;

        // Check market data validity
        if (!validateMarketData(box_market_snapshot, params))
        {
            return false;
        }

        bool return_bool = false;

        while (!return_bool)
        {
            switch (order_data.state)
            {
            case BoxIocStrategyState::IDLE:
                return_bool = handleIdleState(p, s, o, params, order_data, current_time, exe_time);
                break;

            case BoxIocStrategyState::ITM_PENDING:
                return_bool = handleItmPendingState(p, box_market_snapshot, o, params, order_data, current_time, exe_time);
                break;

            case BoxIocStrategyState::ITM_FILLED:
                return_bool = handleItmFilledState(p, box_market_snapshot, o, params, order_data, current_time, exe_time);
                break;

            case BoxIocStrategyState::COMPLETED:
                return_bool = handleCompletedState(p, box_market_snapshot, o, params, order_data, current_time);
                break;
            case BoxIocStrategyState::EXIT:
                return_bool = handleExitState(p, box_market_snapshot, o, params, order_data, current_time, exe_time);
                break;

            default:
                LOG_FILE("BOX_STRATEGY", "Unknown state");
                order_data.state = BoxIocStrategyState::IDLE;
                return false;
            }
        }

        LOG_FILE("BOX_STRATEGY", "Ending with state:" + std::to_string(static_cast<int>(order_data.state)));
        return true;
    }

private:
    static uint64_t getCurrentTimestamp() noexcept
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    static void logStoredMarketDataLatency(const StoredMarketDataLatency &data, const std::string &label) noexcept
    {
        LOG_FILE("BOX_STRATEGY", label + ": start_time=" + std::to_string(data.start_time));

        std::string bids_str = "bids=";
        std::string asks_str = "asks=";
        std::string bids_qty_str = "bids_qty=";
        std::string asks_qty_str = "asks_qty=";
        for (int i = 0; i < 5; ++i)
        {
            bids_str += std::to_string(data.bids[i]) + " ";
            asks_str += std::to_string(data.asks[i]) + " ";
            bids_qty_str += std::to_string(data.bids_qty[i]) + " ";
            asks_qty_str += std::to_string(data.asks_qty[i]) + " ";
        }

        LOG_FILE("BOX_STRATEGY", label + ": " + bids_str);
        LOG_FILE("BOX_STRATEGY", label + ": " + asks_str);
        LOG_FILE("BOX_STRATEGY", label + ": " + bids_qty_str);
        LOG_FILE("BOX_STRATEGY", label + ": " + asks_qty_str);
        LOG_FILE("BOX_STRATEGY", label + ": seqno=" + std::to_string(data.seqno));
        LOG_FILE("BOX_STRATEGY", label + ": msg_type=" + std::string(1, data.msg_type));
        LOG_FILE("BOX_STRATEGY", label + ": internal_seqno=" + std::to_string(data.internal_seqno));
        LOG_FILE("BOX_STRATEGY", label + ": stream_id=" + std::to_string(data.stream_id));
        LOG_FILE("BOX_STRATEGY", label + ": last_traded_price=" + std::to_string(data.last_traded_price));
    }

    static bool validateMarketData(const auto &market_data, const auto &params) noexcept
    {
        logStoredMarketDataLatency(market_data.itm_call, "ITM Call");
        logStoredMarketDataLatency(market_data.otm_put, "OTM Put");
        logStoredMarketDataLatency(market_data.otm_call, "OTM Call");
        logStoredMarketDataLatency(market_data.itm_put, "ITM Put");

        if (params.is_flip_box)
        {
            // Flip Box: requires ITM bids + OTM asks
            if (market_data.itm_call.bids[0] == 0 ||
                market_data.itm_put.bids[0] == 0 ||
                market_data.otm_put.asks[0] == 0 ||
                market_data.otm_call.asks[0] == 0)
            {
                return false;
            }
        }
        else
        {
            // Normal Box: requires ITM asks + OTM bids
            if (market_data.itm_call.asks[0] == 0 ||
                market_data.itm_put.asks[0] == 0 ||
                market_data.otm_put.bids[0] == 0 ||
                market_data.otm_call.bids[0] == 0)
            {
                return false;
            }
        }

        return true;
    }
    static int64_t calculateSpread(const auto &market_data,
                                   const auto &order_data,
                                   bool is_flip_box) noexcept
    {
        int32_t strike_difference = order_data.strike_difference; // in paisa paise

        if (is_flip_box)
        {
            int64_t itm_call_bid = market_data.itm_call.bids[0];
            int64_t itm_put_bid = market_data.itm_put.bids[0];
            int64_t otm_put_ask = market_data.otm_put.asks[0];
            int64_t otm_call_ask = market_data.otm_call.asks[0];

            int64_t spread = (-strike_difference +
                              itm_call_bid + itm_put_bid -
                              otm_put_ask - otm_call_ask);

            LOG_FILE("BOX_STRATEGY",
                     "FlipBox calculation => "
                     "strike_diff:" +
                         std::to_string(strike_difference) +
                         " itm_call_bid:" + std::to_string(itm_call_bid) +
                         " itm_put_bid:" + std::to_string(itm_put_bid) +
                         " otm_put_ask:" + std::to_string(otm_put_ask) +
                         " otm_call_ask:" + std::to_string(otm_call_ask) +
                         " => spread:" + std::to_string(spread));

            return spread;
        }
        else
        {
            int64_t itm_call_ask = market_data.itm_call.asks[0];
            int64_t itm_put_ask = market_data.itm_put.asks[0];
            int64_t otm_put_bid = market_data.otm_put.bids[0];
            int64_t otm_call_bid = market_data.otm_call.bids[0];

            int64_t spread = (strike_difference -
                              itm_call_ask - itm_put_ask +
                              otm_put_bid + otm_call_bid);

            LOG_FILE("BOX_STRATEGY",
                     "NormalBox calculation => "
                     "strike_diff:" +
                         std::to_string(strike_difference) +
                         " itm_call_ask:" + std::to_string(itm_call_ask) +
                         " itm_put_ask:" + std::to_string(itm_put_ask) +
                         " otm_put_bid:" + std::to_string(otm_put_bid) +
                         " otm_call_bid:" + std::to_string(otm_call_bid) +
                         " => spread:" + std::to_string(spread));

            return spread;
        }
    }
    static bool checkSpreadOpportunity(const auto &market_data,
                                       const auto &params,
                                       auto &order_data) noexcept
    {
        int64_t current_spread = calculateSpread(market_data, order_data, order_data.is_flip_box);
        order_data.current_spread = current_spread;

        int64_t target_spread = params.price_difference;

        LOG_FILE("BOX_STRATEGY", "Current spread:" + std::to_string(current_spread) +
                                     " Target spread:" + std::to_string(target_spread));

        return (current_spread >= target_spread);
    }

    static bool handleIdleState(Portfolio &p,
                                const auto &s,
                                OrderManager &o,
                                const auto &params,
                                auto &order_data,
                                uint64_t current_time,
                                const unsigned long long exe_time) noexcept
    {
        StrategyDataLog log;
        if (p.stop_requested)
        {
            p.is_iter_over = true;
            return true;
        }

        resetOrderData(order_data);
        p.updated_tick = false;

        LOG_FILE("BOX_STRATEGY", "IDLE: Box type:" +
                                     std::string(order_data.is_flip_box ? "FLIP" : "NORMAL"));

        // Check spread opportunity
        if (!checkSpreadOpportunity(s.data.box_ioc, params, order_data))
        {
            LOG_FILE("BOX_STRATEGY", "No spread opportunity");
            p.is_iter_over = true;
            return true;
        }

        // Calculate order quantity
        uint32_t current_qty = std::min(params.sol, params.max_lots - order_data.traded_qty);

        // Check OTM liquidity
        uint32_t otm_available = std::min(
            order_data.is_flip_box ? s.data.box_ioc.otm_put.asks_qty[0] : s.data.box_ioc.otm_put.bids_qty[0],
            order_data.is_flip_box ? s.data.box_ioc.otm_call.asks_qty[0] : s.data.box_ioc.otm_call.bids_qty[0]);

        current_qty = std::min(current_qty, otm_available);

        if (current_qty == 0)
        {
            LOG_FILE("BOX_STRATEGY", "Insufficient quantity");
            p.is_iter_over = true;
            return true;
        }

        // Place ITM orders (IOC)
        uint32_t call_itm_price = order_data.is_flip_box ? s.data.box_ioc.itm_call.bids[0] : s.data.box_ioc.itm_call.asks[0];
        uint32_t put_itm_price = order_data.is_flip_box ? s.data.box_ioc.itm_put.bids[0] : s.data.box_ioc.itm_put.asks[0];
        Side call_itm_side = order_data.is_flip_box ? Side::Sell : Side::Buy;
        Side put_itm_side = order_data.is_flip_box ? Side::Sell : Side::Buy;

        // Create two separate Leg objects for ITM orders
        Leg call_itm_leg{params.call_itm_token, call_itm_price, current_qty,
                         call_itm_side, s.data.box_ioc.itm_call.start_time};
        Leg put_itm_leg{params.put_itm_token, put_itm_price, current_qty,
                        put_itm_side, s.data.box_ioc.itm_put.start_time};

        // Send two-leg IOC order
        if (o.sendTwoLegOrder(p.portfolio_id, order_data.call_itm_order_id,
                              order_data.put_itm_order_id, OrderType::IOC,
                              call_itm_leg, put_itm_leg, exe_time, false))
        {
            // Store both order IDs
            order_data.call_itm_pending_qty = current_qty;
            order_data.call_itm_ack = false;
            order_data.put_itm_ack = false;
            order_data.state = BoxIocStrategyState::ITM_PENDING;
            order_data.itm_timer_start = current_time;

            log.msg_type = StrategyState::NewOrder;
            log.pf_id = p.portfolio_id;
            log.oms_order_id = order_data.call_itm_order_id;
            log.token = params.call_itm_token;
            log.price = 0;
            log.qty = current_qty;
            log.side = order_data.is_flip_box ? Side::Sell : Side::Buy;
            log.current_spread = order_data.current_spread;
            log.given_spread = params.price_difference;
            log.diff = 0;
            log.market_snapshot = s;

            LOG_FILE("BOX_STRATEGY", "ITM orders placed - qty:" + std::to_string(current_qty));
            return true;
        }
        else
        {
            LOG_FILE("BOX_STRATEGY", "Failed to place ITM orders");
            return true;
        }
    }

    static bool handleItmPendingState(Portfolio &p,
                                      const auto &s,
                                      OrderManager &o,
                                      const auto &params,
                                      auto &order_data,
                                      uint64_t current_time,
                                      const unsigned long long exe_time) noexcept
    {
        if (!(order_data.call_itm_ack))
        {
            LOG_FILE("BOX_STRATEGY", "Waiting for ITM order ack");
            return true;
        }

        LOG_FILE("BOX_STRATEGY", "ITM_PENDING: filled_qty:" + std::to_string(order_data.call_itm_filled_qty));

        // Check if ITM orders are filled or cancelled
        if (order_data.call_itm_filled_qty > 0)
        {
            LOG_FILE("BOX_STRATEGY", "Moving to ITM_FILLED state as  filled_qty:" + std::to_string(order_data.call_itm_filled_qty));

            order_data.state = BoxIocStrategyState::ITM_FILLED;
            return false; // Continue processing
        }

        return true;
    }

    static bool handleItmFilledState(Portfolio &p,
                                     const auto &s,
                                     OrderManager &o,
                                     const auto &params,
                                     auto &order_data,
                                     uint64_t current_time,
                                     const unsigned long long exe_time) noexcept
    {
        LOG_FILE("BOX_STRATEGY", "ITM_FILLED: Placing OTM orders for qty: " +
                                     std::to_string(order_data.call_itm_filled_qty));

        LOG_FILE("BOX_STRATEGY", "Checking if both OTM legs are fully filled...");
        if (order_data.second_leg_filled_qty >= order_data.call_itm_filled_qty &&
            order_data.third_leg_filled_qty >= order_data.call_itm_filled_qty)
        {
            LOG_FILE("BOX_STRATEGY", "Condition met: Both OTM legs are fully filled.");
            LOG_FILE("BOX_STRATEGY", "second_leg_filled_qty: " + std::to_string(order_data.second_leg_filled_qty));
            LOG_FILE("BOX_STRATEGY", "third_leg_filled_qty: " + std::to_string(order_data.third_leg_filled_qty));
            order_data.state = BoxIocStrategyState::COMPLETED;
            LOG_FILE("BOX_STRATEGY", "Both OTM legs filled, moving to COMPLETED");
            return false;
        }

        // if needed add case of placing order of remaining sol quantity of first leg

        LOG_FILE("BOX_STRATEGY", "Checking if any OTM leg order ID is zero...");
        if ((order_data.second_leg_order_id == 0 && order_data.second_leg_filled_qty < order_data.call_itm_filled_qty) || (order_data.third_leg_order_id == 0 && order_data.third_leg_filled_qty < order_data.call_itm_filled_qty))
        {
            LOG_FILE("BOX_STRATEGY", "Condition met: One or both OTM leg order IDs are zero.");
            placeOtmCoverOrders(p, s, o, params, order_data, current_time, exe_time);
        }

        LOG_FILE("BOX_STRATEGY", "Checking if any OTM leg acknowledgment is received...");
        if (order_data.second_leg_ack || order_data.third_leg_ack)
        {
            LOG_FILE("BOX_STRATEGY", "Condition met: Acknowledgment received for OTM leg(s).");
            handleOtmModification(p, s, o, params, order_data, current_time, exe_time);
        }

        LOG_FILE("BOX_STRATEGY", "OTM leg handling complete. Continuing strategy.");
        return true;
    }

    static void placeOtmCoverOrders(Portfolio &p,
                                    const auto &s,
                                    OrderManager &o,
                                    const auto &params,
                                    auto &order_data,
                                    uint64_t current_time,
                                    const unsigned long long exe_time) noexcept
    {
        uint32_t second_leg_qty = order_data.call_itm_filled_qty - order_data.second_leg_filled_qty;
        uint32_t third_leg_qty = order_data.call_itm_filled_qty - order_data.third_leg_filled_qty;

        // Get OTM prices
        uint32_t third_leg_price = order_data.is_flip_box ? s.otm_call.asks[0] : s.otm_call.bids[0];
        uint32_t second_leg_price = order_data.is_flip_box ? s.otm_put.asks[0] : s.otm_put.asks[0];
        Side call_otm_side = order_data.is_flip_box ? Side::Buy : Side::Sell;
        Side put_otm_side = order_data.is_flip_box ? Side::Buy : Side::Sell;
        if (order_data.second_leg_order_id == 0 && order_data.second_leg_filled_qty < order_data.call_itm_filled_qty)
        {
            // Place PUT OTM order
            Leg put_otm_leg{params.put_otm_token, second_leg_price, second_leg_qty, put_otm_side, s.otm_put.start_time};
            uint32_t second_leg_order_id = 0;

            if (o.sendSingleLegOrder(p.portfolio_id, second_leg_order_id, OrderType::Bidding, put_otm_leg, exe_time, false))
            {
                order_data.second_leg_order_id = second_leg_order_id;
                order_data.second_leg_pending_qty = second_leg_qty;
                order_data.second_leg_price = second_leg_price;
                order_data.second_leg_ack = false;
                order_data.second_otm_timer_start = current_time;
                order_data.second_leg_counter = 0;
                order_data.second_leg_current_fill = 0;
                LOG_FILE("BOX_STRATEGY", "PUT OTM order placed - qty:" + std::to_string(second_leg_qty));
            }
        }

        if (order_data.third_leg_order_id == 0 && order_data.third_leg_filled_qty < order_data.call_itm_filled_qty)
        {
            // Place CALL OTM order
            Leg call_otm_leg{params.call_otm_token, third_leg_price, third_leg_qty, call_otm_side, s.otm_call.start_time};
            uint32_t third_leg_order_id = 0;

            if (o.sendSingleLegOrder(p.portfolio_id, third_leg_order_id, OrderType::Bidding, call_otm_leg, exe_time, false))
            {
                order_data.third_leg_order_id = third_leg_order_id;
                order_data.third_leg_pending_qty = third_leg_qty;
                order_data.third_leg_price = third_leg_price;
                order_data.third_leg_ack = false;
                order_data.third_otm_timer_start = current_time;
                order_data.third_leg_counter = 0;
                order_data.third_leg_current_fill = 0;

                LOG_FILE("BOX_STRATEGY", "CALL OTM order placed - qty:" + std::to_string(third_leg_qty));
            }
        }
    }

    static void handleOtmModification(Portfolio &p,
                                      const auto &s,
                                      OrderManager &o,
                                      const auto &params,
                                      auto &order_data,
                                      uint64_t current_time,
                                      const unsigned long long exe_time) noexcept
    {
        uint32_t price_level = 0;
        uint64_t time_elapsed = current_time - order_data.second_otm_timer_start;
        uint32_t second_leg_qty = order_data.call_itm_filled_qty - order_data.second_leg_filled_qty + order_data.second_leg_current_fill;
        uint32_t third_leg_qty = order_data.call_itm_filled_qty - order_data.third_leg_filled_qty + order_data.third_leg_current_fill;

        LOG_FILE("BOX_STRATEGY", "Checking PUT OTM modification...");
        LOG_FILE("BOX_STRATEGY", "PUT OTM time_elapsed: " + std::to_string(time_elapsed) +
                                     ", timer threshold: " + std::to_string(params.timer_ms * 1000) +
                                     ", pending_qty: " + std::to_string(order_data.second_leg_pending_qty) +
                                     ", filled_qty: " + std::to_string(order_data.second_leg_filled_qty));

        if (order_data.second_leg_ack && time_elapsed >= params.timer_ms * 1000)
        {
            price_level = (order_data.third_leg_counter < 4) ? 0 : 1;
            uint32_t new_price = order_data.is_flip_box ? s.otm_put.asks[price_level] : s.otm_put.bids[price_level];

            LOG_FILE("BOX_STRATEGY", "PUT OTM new_price: " + std::to_string(new_price) +
                                         ", current_price: " + std::to_string(order_data.second_leg_price));

            if ((new_price != order_data.second_leg_price && new_price > 0) || (order_data.second_leg_last_pending_qty != second_leg_qty))
            {
                Side side = order_data.is_flip_box ? Side::Buy : Side::Sell;

                LOG_FILE("BOX_STRATEGY", "PUT OTM modifying order with second_leg_qty: " +
                                             std::to_string(second_leg_qty) + ", side: " +
                                             (side == Side::Buy ? "Buy" : "Sell"));

                Leg put_leg{params.put_otm_token, new_price, second_leg_qty, side, s.otm_put.start_time};

                if (o.sendModifyPlacement(p.portfolio_id, order_data.second_leg_order_id, put_leg, exe_time))
                {
                    order_data.second_leg_price = new_price;
                    order_data.second_leg_ack = false;
                    order_data.second_otm_timer_start = current_time;
                    order_data.second_leg_counter++;
                    order_data.second_leg_pending_qty = second_leg_qty;
                    order_data.second_leg_price = new_price;

                    LOG_FILE("BOX_STRATEGY", "PUT OTM modification successful. Updated price: " +
                                                 std::to_string(new_price) + ", counter: " +
                                                 std::to_string(order_data.second_leg_counter));
                }
                else
                {
                    LOG_FILE("BOX_STRATEGY", "PUT OTM modification failed.");
                }
            }
        }

        time_elapsed = current_time - order_data.third_otm_timer_start;

        LOG_FILE("BOX_STRATEGY", "Checking CALL OTM modification...");
        LOG_FILE("BOX_STRATEGY", "CALL OTM time_elapsed: " + std::to_string(time_elapsed) +
                                     ", timer threshold: " + std::to_string(params.timer_ms * 1000) +
                                     ", pending_qty: " + std::to_string(order_data.third_leg_pending_qty) +
                                     ", filled_qty: " + std::to_string(order_data.third_leg_filled_qty));

        if (order_data.third_leg_ack && time_elapsed >= params.timer_ms * 1000)
        {
            price_level = (order_data.third_leg_counter < 4) ? 0 : 1;
            uint32_t new_price = order_data.is_flip_box ? s.otm_call.asks[price_level] : s.otm_call.bids[price_level];

            LOG_FILE("BOX_STRATEGY", "CALL OTM new_price: " + std::to_string(new_price) +
                                         ", current_price: " + std::to_string(order_data.third_leg_price));

            if ((new_price != order_data.third_leg_price && new_price > 0) || (order_data.third_leg_last_pending_qty != third_leg_qty))
            {
                Side side = order_data.is_flip_box ? Side::Buy : Side::Sell;

                LOG_FILE("BOX_STRATEGY", "CALL OTM modifying order with remaining_qty: " +
                                             std::to_string(third_leg_qty) + ", side: " +
                                             (side == Side::Buy ? "Buy" : "Sell"));

                Leg call_leg{params.call_otm_token, new_price, third_leg_qty, side, s.otm_call.start_time};

                if (o.sendModifyPlacement(p.portfolio_id, order_data.third_leg_order_id, call_leg, exe_time))
                {
                    order_data.third_leg_price = new_price;
                    order_data.third_leg_ack = false;
                    order_data.third_otm_timer_start = current_time;
                    order_data.third_leg_counter++;
                    order_data.third_leg_pending_qty = third_leg_qty;
                    order_data.third_leg_price = new_price;

                    LOG_FILE("BOX_STRATEGY", "CALL OTM modification successful. Updated price: " +
                                                 std::to_string(new_price) + ", counter: " +
                                                 std::to_string(order_data.third_leg_counter));
                }
                else
                {
                    LOG_FILE("BOX_STRATEGY", "CALL OTM modification failed.");
                }
            }
        }
    }

    static bool handleCompletedState(Portfolio &p,
                                     const auto &s,
                                     OrderManager &o,
                                     const auto &params,
                                     auto &order_data,
                                     uint64_t current_time) noexcept
    {
        p.is_iter_over = true;

        // Update traded quantity
        // order_data.traded_qty += order_data.call_itm_filled_qty;// wrong may be
        order_data.traded_qty += std::min(order_data.second_leg_filled_qty, order_data.third_leg_filled_qty);
        p.traded_qty = order_data.traded_qty;
        p.is_data_updated = true;

        // Calculate achieved spread
        // calculateAchievedSpread(order_data);

        LOG_FILE("BOX_STRATEGY", "COMPLETED: traded_qty:" + std::to_string(order_data.traded_qty));

        // Check if we can place another cycle
        if (order_data.traded_qty >= params.max_lots)
        {
            LOG_FILE("BOX_STRATEGY", "Max lots reached, terminating");
            p.is_active = 0;
            p.terminate = 1;
            return true;
        }

        // Reset for next cycle
        resetOrderData(order_data);
        order_data.state = BoxIocStrategyState::IDLE;
        return true;
    }

    static bool handleExitState(Portfolio &p,
                                const auto &s,
                                OrderManager &o,
                                const auto &params,
                                auto &order_data,
                                uint64_t current_time,
                                const unsigned long long exe_time) noexcept
    {
        p.stop_requested = true;

        LOG_FILE("BOX_STRATEGY", "EXIT: Placing OTM orders for qty: " +
                                     std::to_string(order_data.call_itm_filled_qty));
        if (order_data.call_itm_filled_qty > 0 && order_data.call_itm_filled_qty > std::min(order_data.second_leg_filled_qty, order_data.third_leg_filled_qty))
        {
        }

        LOG_FILE("BOX_STRATEGY", "Checking if both OTM legs are fully filled...");
        if (order_data.second_leg_filled_qty >= order_data.call_itm_filled_qty &&
            order_data.third_leg_filled_qty >= order_data.call_itm_filled_qty)
        {
            LOG_FILE("BOX_STRATEGY", "Condition met: Both OTM legs are fully filled.");
            LOG_FILE("BOX_STRATEGY", "second_leg_filled_qty: " + std::to_string(order_data.second_leg_filled_qty));
            LOG_FILE("BOX_STRATEGY", "third_leg_filled_qty: " + std::to_string(order_data.third_leg_filled_qty));
            order_data.state = BoxIocStrategyState::COMPLETED;
            LOG_FILE("BOX_STRATEGY", "Both OTM legs filled, moving to COMPLETED");
            return false;
        }

        // if needed add case of placing order of remaining sol quantity of first leg

        LOG_FILE("BOX_STRATEGY", "Checking if any OTM leg order ID is zero...");
        if (order_data.second_leg_order_id == 0 || order_data.third_leg_order_id == 0)
        {
            LOG_FILE("BOX_STRATEGY", "Condition met: One or both OTM leg order IDs are zero.");
            placeOtmCoverOrders(p, s, o, params, order_data, current_time, exe_time);
        }

        LOG_FILE("BOX_STRATEGY", "Checking if any OTM leg acknowledgment is received...");
        if (order_data.second_leg_ack || order_data.third_leg_ack)
        {
            LOG_FILE("BOX_STRATEGY", "Condition met: Acknowledgment received for OTM leg(s).");
            handleOtmModification(p, s, o, params, order_data, current_time, exe_time);
        }

        LOG_FILE("BOX_STRATEGY", "OTM leg handling complete. Continuing strategy.");
        return true;
    }

    static void resetOrderData(auto &order_data) noexcept
    {
        // Reset order IDs and quantities
        order_data.put_itm_order_id = 0;
        order_data.call_itm_order_id = 0;
        order_data.call_itm_filled = false;

        // order_data.itm_order_id = 0;
        order_data.second_leg_order_id = 0;
        order_data.third_leg_order_id = 0;

        order_data.call_itm_pending_qty = 0;
        order_data.put_itm_pending_qty = 0;
        order_data.call_itm_filled_qty = 0;
        order_data.second_leg_pending_qty = 0;
        order_data.second_leg_filled_qty = 0;
        order_data.third_leg_pending_qty = 0;
        order_data.third_leg_filled_qty = 0;

        // Reset values for spread calculation
        // order_data.call_itm_value = 0;
        // order_data.put_itm_value = 0;
        // order_data.call_otm_value = 0;
        // order_data.put_otm_value = 0;

        // Reset flags
        // order_data.itm_ack = true;
        order_data.call_itm_ack = true;
        order_data.put_itm_ack = true;
        order_data.second_leg_ack = true;
        order_data.third_leg_ack = true;

        // Reset timers and counters
        // order_data.otm_modification_count = 0;
        order_data.second_leg_counter = 0;
        order_data.third_leg_counter = 0;
    }

public:
    // Order Event Handlers
    static void handleOrderFill(Portfolio &p, uint32_t order_id, uint32_t filled_qty,
                                uint32_t fill_price, OrderManager &order_manager,
                                unsigned long long exe_time) noexcept
    {
        auto &order_data = p.order_data.box_ioc;

        // Check if it's one of the ITM orders
        if (order_id == order_data.call_itm_order_id)
        {
            handleCallItmOrderFill(p, order_data, filled_qty, fill_price);
        }
        else if (order_id == order_data.put_itm_order_id)
        {
            handlePutItmOrderFill(p, order_data, filled_qty, fill_price);
        }
        else if (order_id == order_data.second_leg_order_id)
        {
            handleSecondLegFill(p, order_data, filled_qty, fill_price);
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            handleThirdLegFill(p, order_data, filled_qty, fill_price);
        }
    }

    static void handlePartialFill(Portfolio &p, uint32_t order_id, uint32_t filled_qty,
                                  uint32_t remaining_qty, uint32_t fill_price,
                                  OrderManager &order_manager,
                                  unsigned long long exe_time) noexcept
    {
        // Same as full fill for our tracking
        handleOrderFill(p, order_id, filled_qty, fill_price, order_manager, exe_time);
    }

    static void handleOrderCancel(Portfolio &p, uint32_t order_id) noexcept
    {
        auto &order_data = p.order_data.box_ioc;

        if (order_id == order_data.call_itm_order_id)
        {
            order_data.call_itm_cancelled = true;
            if (order_data.call_itm_filled != true)
            {

                order_data.call_itm_ack = true;
                order_data.state = BoxIocStrategyState::IDLE;
                resetOrderData(order_data);
            }
            LOG_FILE("BOX_STRATEGY", "ITM Call order cancelled");
        }
        else if (order_id == order_data.put_itm_order_id)
        {
            order_data.put_itm_cancelled = true;
            order_data.put_itm_ack = true;
            LOG_FILE("BOX_STRATEGY", "ITM Put order cancelled");
        }
        else if (order_id == order_data.second_leg_order_id)
        {
            order_data.second_leg_order_id = 0;
            order_data.second_leg_pending_qty = 0;
            LOG_FILE("BOX_STRATEGY", "PUT OTM order cancelled");
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            order_data.third_leg_order_id = 0;
            order_data.third_leg_pending_qty = 0;
            LOG_FILE("BOX_STRATEGY", "CALL OTM order cancelled");
        }
    }

    static void handleOrderReject(Portfolio &p, uint32_t order_id) noexcept
    {
        auto &order_data = p.order_data.box_ioc;
        LOG_FILE("BOX_STRATEGY", "Order rejected - id:" + std::to_string(order_id));
        // TODO:
        // if (order_id == order_data.call_itm_order_id)
        // {
        // order_data.state = BoxIocStrategyState::IDLE;
        // resetOrderData(order_data);
        // }
        if (order_id == order_data.put_itm_order_id)
        {
            // order_data.state = BoxIocStrategyState::IDLE;
            // resetOrderData(order_data);
            LOG_FILE("BOX_STRATEGY", "PUT ITM order reject received");
        }
        else if (order_id == order_data.call_itm_order_id)
        {
            order_data.state = BoxIocStrategyState::IDLE;
            resetOrderData(order_data);
            LOG_FILE("BOX_STRATEGY", "CALL ITM order reject received");
        }
        else if (order_id == order_data.second_leg_order_id)
        {
            order_data.second_leg_order_id = 0;
            order_data.second_leg_ack = true;
            LOG_FILE("BOX_STRATEGY", "PUT OTM order reject received");
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            order_data.third_leg_order_id = 0;
            order_data.third_leg_ack = true;
            LOG_FILE("BOX_STRATEGY", "CALL OTM order reject received");
        }
        LOG_FILE("BOX_STRATEGY", "Exiting this fun");
    }

    static void handleOrderFailed(Portfolio &p, uint32_t order_id) noexcept
    {
        auto &order_data = p.order_data.box_ioc;
        LOG_FILE("BOX_STRATEGY", "Order Failed - id:" + std::to_string(order_id));
        // TODO:
        // if (order_id == order_data.call_itm_order_id)
        // {
        // order_data.state = BoxIocStrategyState::IDLE;
        // resetOrderData(order_data);
        // }
        if (order_id == order_data.put_itm_order_id)
        {
            // order_data.state = BoxIocStrategyState::IDLE;
            // resetOrderData(order_data);
            LOG_FILE("BOX_STRATEGY", "PUT ITM order Failed received");
        }
        else if (order_id == order_data.call_itm_order_id)
        {
            order_data.state = BoxIocStrategyState::EXIT;
            order_data.call_itm_ack = true;
            // resetOrderData(order_data);
            LOG_FILE("BOX_STRATEGY", "CALL ITM order Failed received moving to exit state");
        }
        else if (order_id == order_data.second_leg_order_id)
        {
            order_data.second_leg_order_id = 0;
            order_data.second_leg_ack = true;
            LOG_FILE("BOX_STRATEGY", "PUT OTM order Failed received");
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            order_data.third_leg_order_id = 0;
            order_data.third_leg_ack = true;
            LOG_FILE("BOX_STRATEGY", "CALL OTM order Failed received");
        }
    }

    static void handleNewAck(Portfolio &p, uint32_t order_id) noexcept
    {
        auto &order_data = p.order_data.box_ioc;

        if (order_id == order_data.put_itm_order_id)
        {
            order_data.put_itm_ack = true;
            LOG_FILE("BOX_STRATEGY", "PUT OTM order ack received");
        }
        else if (order_id == order_data.call_itm_order_id)
        {
            order_data.call_itm_ack = true;
            LOG_FILE("BOX_STRATEGY", "CALL OTM order ack received");
        }
        else if (order_id == order_data.second_leg_order_id)
        {
            order_data.second_leg_ack = true;
            order_data.second_leg_last_pending_qty = order_data.second_leg_pending_qty;
            order_data.second_leg_last_price = order_data.second_leg_price;
            LOG_FILE("BOX_STRATEGY", "PUT OTM order ack received");
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            order_data.third_leg_ack = true;
            order_data.third_leg_last_pending_qty = order_data.third_leg_pending_qty;
            order_data.third_leg_last_price = order_data.third_leg_price;

            LOG_FILE("BOX_STRATEGY", "CALL OTM order ack received");
        }
    }

    static void handleModifyAck(Portfolio &p, uint32_t order_id) noexcept
    {
        auto &order_data = p.order_data.box_ioc;

        if (order_id == order_data.second_leg_order_id)
        {
            order_data.second_leg_ack = true;
            order_data.second_leg_last_pending_qty = order_data.second_leg_pending_qty;
            order_data.second_leg_last_price = order_data.second_leg_price;
            LOG_FILE("BOX_STRATEGY", "PUT OTM modify ack received");
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            order_data.third_leg_ack = true;
            order_data.third_leg_last_pending_qty = order_data.third_leg_pending_qty;
            order_data.third_leg_last_price = order_data.third_leg_price;

            LOG_FILE("BOX_STRATEGY", "CALL OTM modify ack received");
        }
    }

    static void handleModifyReject(Portfolio &p, uint32_t order_id) noexcept
    {
        auto &order_data = p.order_data.box_ioc;

        if (order_id == order_data.second_leg_order_id)
        {
            order_data.second_leg_ack = true;
            LOG_FILE("BOX_STRATEGY", "PUT OTM modify rejected");
        }
        else if (order_id == order_data.third_leg_order_id)
        {
            order_data.third_leg_ack = true;
            LOG_FILE("BOX_STRATEGY", "CALL OTM modify rejected");
        }
    }

    static void handleCancelReject(Portfolio &p, uint32_t order_id) noexcept
    {
        LOG_FILE("BOX_STRATEGY", "Cancel rejected for order:" + std::to_string(order_id));
    }

    static bool handleStrategyComplete(Portfolio &strat)
    {
        LOG_FILE("BOX_STRATEGY", "Came in function");

        auto &params = strat.params.box_ioc;
        auto &order_data = strat.order_data.box_ioc;
        LOG_FILE("BOX_STRATEGY", "Came in function2");

        bool is_terminated = strat.terminate;
        bool stop_completed = (strat.stop_requested && strat.is_iter_over);
        LOG_FILE("BOX_STRATEGY", "Came in function3");

        if (is_terminated || stop_completed)
        {
            strat.is_active = false;
            // LOG_FILE("BOX_STRATEGY", "Strategy completed");
            return true;
        }
        LOG_FILE("BOX_STRATEGY", "Came in function4");

        return false;
    }

private:
    static void handleCallItmOrderFill(Portfolio &p, auto &order_data,
                                       uint32_t filled_qty, uint32_t fill_price) noexcept
    {
        order_data.call_itm_filled_qty += filled_qty;
        order_data.call_itm_ack = true;
        order_data.call_itm_filled = true;
        p.is_iter_over = false;

        // Track value for achieved spread calculation
        // order_data.v1_call += filled_qty * fill_price;
        // order_data.q1_call += filled_qty;
        // order_data.itm_call_price = fill_price;

        LOG_FILE("BOX_STRATEGY", "CALL ITM filled - qty:" + std::to_string(filled_qty) +
                                     " price:" + std::to_string(fill_price) +
                                     " total call_itm_filled_qty:" + std::to_string(order_data.call_itm_filled_qty));
    }

    static void handlePutItmOrderFill(Portfolio &p, auto &order_data,
                                      uint32_t filled_qty, uint32_t fill_price) noexcept
    {
        order_data.put_itm_filled_qty += filled_qty;
        order_data.put_itm_ack = true;
        order_data.put_itm_filled = true;
        p.is_iter_over = false;

        // // Track value for achieved spread calculation
        // order_data.v4_put += filled_qty * fill_price;
        // order_data.q4_put += filled_qty;
        // order_data.itm_put_price = fill_price;

        LOG_FILE("BOX_STRATEGY", "PUT ITM filled - qty:" + std::to_string(filled_qty) +
                                     " price:" + std::to_string(fill_price) +
                                     " total put_itm_filled_qty:" + std::to_string(order_data.put_itm_filled_qty));
    }

    static void handleSecondLegFill(Portfolio &p, auto &order_data,
                                    uint32_t filled_qty, uint32_t fill_price) noexcept
    {
        // Second leg is OTM Put
        order_data.second_leg_filled_qty += filled_qty;
        order_data.second_leg_timer_identifier = true;
        order_data.second_leg_ack = true;
        order_data.second_leg_current_fill += filled_qty;

        LOG_FILE("BOX_STRATEGY", "Second leg (OTM PUT) filled - qty:" + std::to_string(filled_qty) +
                                     " price:" + std::to_string(fill_price) +
                                     " total_filled:" + std::to_string(order_data.second_leg_filled_qty));

        if (order_data.second_leg_current_fill >= order_data.second_leg_last_pending_qty)
        {
            order_data.second_leg_order_id = 0;
            order_data.second_leg_pending_qty = 0;
            order_data.second_leg_last_pending_qty = 0;
        }
    }

    static void handleThirdLegFill(Portfolio &p, auto &order_data,
                                   uint32_t filled_qty, uint32_t fill_price) noexcept
    {
        // Third leg is OTM Call
        order_data.third_leg_filled_qty += filled_qty;
        order_data.third_leg_timer_identifier = true;
        order_data.third_leg_ack = true;
        order_data.third_leg_current_fill += filled_qty;

        // // Track value for achieved spread
        // order_data.v3_call += filled_qty * fill_price;
        // order_data.q3_call += filled_qty;

        LOG_FILE("BOX_STRATEGY", "Third leg (OTM CALL) filled - qty:" + std::to_string(filled_qty) +
                                     " price:" + std::to_string(fill_price) +
                                     " total_filled:" + std::to_string(order_data.third_leg_filled_qty));

        if (order_data.third_leg_current_fill >= order_data.third_leg_last_pending_qty)
        {
            order_data.third_leg_order_id = 0;
            order_data.third_leg_pending_qty = 0;
            order_data.third_leg_last_pending_qty = 0;
        }
    }
};
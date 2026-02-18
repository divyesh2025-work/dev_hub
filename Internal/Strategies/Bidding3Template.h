#pragma once
#include "TemplateStrategy.h"
#include <climits>

template <>
struct StrategyExecutor<StrategyKind::CONREV_BID>
{
    static bool run(Portfolio &p,
                    const StrategyMarketSnapshot &s,
                    OrderManager &o, const unsigned long long exe_time) noexcept
    {

        auto &params = p.params.three_leg_bidding;
        auto &order_data = p.order_data.three_leg_bidding;

        LOG_FILE("3LEGBIDDING", "Starting bidding with state:" + std::to_string(static_cast<int>(order_data.state)));
        LOG_FILE("3LEGBIDDING", "Traded and max lots:" + std::to_string(order_data.traded_qty) + "," + std::to_string(params.max_lots));
        LOG_FILE("3LEGBIDDING", "Leg1  , Leg2 and leg3 filled qty2:" + std::to_string(order_data.leg1_filled_qty) + "," + std::to_string(order_data.leg2_filled_qty) + " , " + std::to_string(order_data.leg3_filled_qty));

        order_data.is_conversion = p.params.three_leg_bidding.con_flag;

        if (__builtin_expect(order_data.traded_qty >= params.max_lots, 0)) [[unlikely]]
        {
            p.is_active = 0;
            p.terminate = 1;
            LOG_LIVE("3LEGBIDDING", "Traded greater that max lots");

            return true;
        }

        LOG_FILE("3legbidding", "came here new max:" + std::to_string(order_data.new_max) + " , mod max:" + std::to_string(order_data.mod_max));

        // Get current timestamp
        const uint64_t current_time = getCurrentTimestamp();

        // Read market data once
        const uint32_t fut_bid = s.data.three_leg_bidding.fut.bids[0], fut_ask = s.data.three_leg_bidding.fut.asks[0];
        const uint32_t call_bid = s.data.three_leg_bidding.call.bids[0], call_ask = s.data.three_leg_bidding.call.asks[0];
        const uint32_t put_bid = s.data.three_leg_bidding.put.bids[0], put_ask = s.data.three_leg_bidding.put.asks[0];
        const uint32_t strike = order_data.strike_price;

        // checking for 0 only for required things not all as on Expiry all bids are null
        if ((params.con_flag && (fut_ask == 0 || call_bid == 0 || put_ask == 0)) || (!params.con_flag && (fut_bid == 0 || call_ask == 0 || put_bid == 0)))
        {
            std::cerr << "All data not available for pf id:" << p.portfolio_id << ",conflag:" << params.con_flag << "," << std::to_string(fut_bid) + "," + std::to_string(fut_ask) + "," + std::to_string(call_bid) + "," + std::to_string(call_ask) + "," + std::to_string(put_bid) + "," + std::to_string(put_ask) << "\n";
            LOG_FILE("3LEGBIDDING", "somethings are zero");
            LOG_FILE("3LEGBIDDING", std::to_string(fut_bid) + "," + std::to_string(fut_ask) + "," + std::to_string(call_bid) + "," + std::to_string(call_ask) + "," + std::to_string(put_bid) + "," + std::to_string(put_ask));
            LOG_LIVE("3LEGBIDDING", std::to_string(fut_bid) + "," + std::to_string(fut_ask) + "," + std::to_string(call_bid) + "," + std::to_string(call_ask) + "," + std::to_string(put_bid) + "," + std::to_string(put_ask));
            return false;
        }

        // LOG_FILE("3LEGBIDDING", "Future Data");
        // printMarketData(s.data.three_leg_bidding.fut, p);
        // LOG_FILE("3LEGBIDDING", "Call Data");
        // printMarketData(s.data.three_leg_bidding.call, p);
        // LOG_FILE("3LEGBIDDING", "Put Data");
        // printMarketData(s.data.three_leg_bidding.put, p);
        // LOG_FILE("3LEGBIDDING", "Came here2");
        bool return_bool = false;
        while (!return_bool)
        {

            switch (order_data.state)
            {
            case ThreeLegBiddingState::IDLE: // if only if 1st leg na mare than iter over else iter over
            {

                return_bool = handleIdleState(p, s, o, params, order_data, current_time,
                                              fut_bid, fut_ask, call_bid, call_ask, put_bid, put_ask, strike, exe_time); // YES
                break;
            }
            case ThreeLegBiddingState::LEG1_PENDING: // strictly no to iter over,if stop req cancel order
            {

                return_bool = handleLeg1PendingState(p, s, o, params, order_data, current_time,
                                                     fut_bid, fut_ask, call_bid, call_ask, put_bid, put_ask, strike, exe_time); // Y /N
                break;
            }
            case ThreeLegBiddingState::LEG1_PARTIAL_FILLED: // strictly no iter over , if stop req than cacel remain
            {

                return_bool = handleLeg1PartialFilledState(p, s, o, params, order_data, current_time,
                                                           fut_bid, fut_ask, call_bid, call_ask, put_bid, put_ask, strike, exe_time); // Y / N

                break;
            }
            case ThreeLegBiddingState::LEG1_FILLED: // its not in your handle now iter never ends from here
            {

                return_bool = handleLeg1FilledState(p, s, o, params, order_data, current_time,
                                                    fut_bid, fut_ask, call_bid, call_ask, put_bid, put_ask, strike, exe_time); // Y / N
                break;
            }
            case ThreeLegBiddingState::LEGS23_PENDING: // its not in your handle now iter never ends from here
            {

                return_bool = handleLegs23PendingState(p, s, o, params, order_data, current_time,
                                                       fut_bid, fut_ask, call_bid, call_ask, put_bid, put_ask, strike, exe_time); // Y N
                break;
            }
            case ThreeLegBiddingState::COMPLETED: // iter over here
            {

                return_bool = handleCompletedState(p, s, o, params, order_data, current_time); // NO
                break;
            }
            case ThreeLegBiddingState::EXIT: //
            {

                return_bool = handleExitState(p, s, o, params, order_data, current_time); // NO
                break;
            }
            default:
            {
                LOG_FILE("3LEGBIDDING", "Unknown state came");
                LOG_LIVE("3LEGBIDDING", "Unknown state came:" + std::to_string(static_cast<int>(order_data.state)));
                order_data.state = ThreeLegBiddingState::IDLE;
                return false;
            }
            }
        }

        order_data.traded_qty = getTotalTradedQty(order_data);
        p.traded_qty = order_data.traded_qty;
        LOG_FILE("3LEGBIDDING", "Ending bidding with state:" + std::to_string(static_cast<int>(order_data.state)));

        return true;
    }

private:
    static uint64_t getCurrentTimestamp() noexcept
    {
        uint32_t lo, hi;
        __asm__ __volatile__(
            "rdtsc"
            : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }
    static uint32_t getTotalTradedQty(const auto &order_data) noexcept
    {
        return std::min(order_data.leg2_over_all_fill, order_data.leg3_over_all_fill);
    }

    // Helper functions to calculate coverage states
    static uint32_t getTotalCoveredQty(const auto &order_data) noexcept
    {
        return std::min(order_data.leg2_covered_qty, order_data.leg3_covered_qty);
    }

    static uint32_t getUncoveredQty(const auto &order_data) noexcept
    {
        const uint32_t total_covered = getTotalCoveredQty(order_data);
        return order_data.leg1_filled_qty > total_covered ? order_data.leg1_filled_qty - total_covered : 0;
    }

    static uint32_t getTotalCoveredFilledQty(const auto &order_data) noexcept
    {

        return std::min({order_data.leg1_filled_qty, order_data.leg2_actual_fill_qty, order_data.leg3_actual_fill_qty});
    }

    static uint32_t getLeg2UncoveredQty(const auto &order_data) noexcept
    {
        return order_data.leg1_filled_qty > order_data.leg2_covered_qty ? order_data.leg1_filled_qty - order_data.leg2_covered_qty : 0;
    }

    static uint32_t getLeg3UncoveredQty(const auto &order_data) noexcept
    {
        return order_data.leg1_filled_qty > order_data.leg3_covered_qty ? order_data.leg1_filled_qty - order_data.leg3_covered_qty : 0;
    }

    static bool checkSpreadOpportunity(const Portfolio &p,
                                       uint32_t fut_price, uint32_t call_price, uint32_t put_price,
                                       uint32_t strike, bool is_conversion, const auto &params, int &spread) noexcept
    {
        int64_t current_spread;
        int64_t threshold = int64_t(params.leg1_spread_threshold);

        if (is_conversion)
        {
            // Conversion: Strike - Future + Call - Put
            current_spread = int64_t(strike) - int64_t(fut_price) + int64_t(call_price) - int64_t(put_price);
        }
        else
        {
            // Reversal: -Strike + Future - Call + Put
            current_spread = -int64_t(strike) + int64_t(fut_price) - int64_t(call_price) + int64_t(put_price);
        }

        spread = current_spread;
        return current_spread >= threshold;
    }

    static ALWAYS_INLINE uint32_t calculateLeg1Price(LegType bid_type,
                                                     uint32_t strike,
                                                     uint32_t price_cover1,
                                                     uint32_t price_cover2,
                                                     bool is_conversion,
                                                     const auto &params,
                                                     uint32_t token) noexcept
    {
        int32_t offset = helper::get_bid_interval(token);
        int64_t price = 0;

        LOG_FILE("3LEGBIDDING", "Calculating Leg1 Price");
        LOG_FILE("3LEGBIDDING", "Inputs: bid_type=" + std::to_string(static_cast<int>(bid_type)) +
                                    ", strike=" + std::to_string(strike) +
                                    ", price_cover1=" + std::to_string(price_cover1) +
                                    ", price_cover2=" + std::to_string(price_cover2) +
                                    ", is_conversion=" + std::string(is_conversion ? "true" : "false") +
                                    ", spread=" + std::to_string(params.spread) +
                                    ", token=" + std::to_string(token) +
                                    ", offset=" + std::to_string(offset));

        if (bid_type == LegType::Future)
        {
            price = int64_t(strike) +
                    (is_conversion ? -int64_t(params.spread) : int64_t(params.spread)) +
                    int64_t(price_cover1) - int64_t(price_cover2);
            LOG_FILE("3LEGBIDDING", "Future Price Calculation: " + std::to_string(price));
        }
        else if (bid_type == LegType::Call)
        {
            price = -int64_t(strike) +
                    (is_conversion ? int64_t(params.spread) : -int64_t(params.spread)) +
                    int64_t(price_cover1) + int64_t(price_cover2);
            LOG_FILE("3LEGBIDDING", "Call Price Calculation: " + std::to_string(price));
        }
        else
        {
            price = int64_t(strike) +
                    (is_conversion ? -int64_t(params.spread) : int64_t(params.spread)) +
                    int64_t(price_cover2) - int64_t(price_cover1);
            LOG_FILE("3LEGBIDDING", "Put Price Calculation: " + std::to_string(price));
        }

        int64_t before_rounding = price;
        price = is_conversion
                    ? price - (price % offset)                        // round down for conversion
                    : price + ((offset - (price % offset)) % offset); // round up for reversion

        LOG_FILE("3LEGBIDDING", "Before Rounding: " + std::to_string(before_rounding) +
                                    ", After Rounding: " + std::to_string(price));

        uint32_t final_price = price > 0 ? uint32_t(price) : 0;
        LOG_FILE("3LEGBIDDING", "Final Leg1 Price: " + std::to_string(final_price));

        return final_price;
    }

    static bool handleIdleState(Portfolio &p,
                                const StrategyMarketSnapshot &s,
                                OrderManager &o,
                                const auto &params,
                                auto &order_data,
                                uint64_t current_time,
                                uint32_t fut_bid, uint32_t fut_ask,
                                uint32_t call_bid, uint32_t call_ask,
                                uint32_t put_bid, uint32_t put_ask,
                                uint32_t strike, const unsigned long long exe_time) noexcept
    {
        StrategyDataLog log;
        if (p.stop_requested) [[unlikely]]
        {
            p.is_iter_over = true;
            return true;
        }
        resetOrderData(order_data);
        p.updated_tick = false;
        LOG_FILE("3LEGBIDDING", "IN IDLE");
        bool is_conversion = order_data.is_conversion;
        int spread = INT_MAX;
        if (params.is_opportunity) // LIKELY
        {
            bool opportunity = checkSpreadOpportunity(p, is_conversion ? fut_ask : fut_bid,
                                                      is_conversion ? call_bid : call_ask,
                                                      is_conversion ? put_ask : put_bid, strike, is_conversion, params, spread);
            if (!opportunity)
            {
                LOG_FILE("3LEGBIDDING", "Opprotunity is not there");
                p.is_iter_over = true;
                return true;
            }
        }

        uint32_t available_qty = 0;
        uint32_t leg1_price = 0;

        const auto &bid_leg = p.legs[0];    // bidding leg
        const auto &cover_leg1 = p.legs[1]; // cover leg 1
        const auto &cover_leg2 = p.legs[2]; // cover leg 2

        // Map legs to market data pointers for fast access
        const StoredMarketDataLatency *bid_data = nullptr;
        const StoredMarketDataLatency *cover_data1 = nullptr;
        const StoredMarketDataLatency *cover_data2 = nullptr;

        auto &legs_data = s.data.three_leg_bidding;
        uint32_t fut_token = 0;

        if (bid_leg.leg_type == LegType::Future)
        {
            fut_token = bid_leg.symbol_token;
            bid_data = &legs_data.fut;
            cover_data1 = &legs_data.call;
            cover_data2 = &legs_data.put;
        }
        else if (bid_leg.leg_type == LegType::Call)
        {
            bid_data = &legs_data.call;
            cover_data1 = &legs_data.fut;
            cover_data2 = &legs_data.put;
            fut_token = cover_leg1.leg_type == LegType::Future ? cover_leg1.symbol_token : cover_leg2.symbol_token;
        }
        else
        {
            bid_data = &legs_data.put;
            cover_data1 = &legs_data.fut;
            cover_data2 = &legs_data.call;
            fut_token = cover_leg1.leg_type == LegType::Future ? cover_leg1.symbol_token : cover_leg2.symbol_token;
        }

        available_qty = std::min(cover_leg1.side == OrderSide::Buy ? cover_data1->asks_qty[0] : cover_data1->bids_qty[0], cover_leg2.side == OrderSide::Buy ? cover_data2->asks_qty[0] : cover_data2->bids_qty[0]);
        leg1_price = calculateLeg1Price(bid_leg.leg_type, strike,
                                        cover_leg1.side == OrderSide::Buy ? cover_data1->asks[0] : cover_data1->bids[0],
                                        cover_leg2.side == OrderSide::Buy ? cover_data2->asks[0] : cover_data2->bids[0],
                                        params.con_flag, params, fut_token);

        if (leg1_price == 0) [[unlikely]]
        { // unlikely
            LOG_FILE("3LEGBIDDING", "Leg1 Price is zero");
            std::cerr << "Leg1 Price is 0 for pf id:" << p.portfolio_id << "\n";
            p.is_iter_over = true;
            return true;
        }

        uint32_t order_qty = std::min({static_cast<uint32_t>(params.sol),
                                       params.max_lots - order_data.traded_qty,
                                       available_qty});

        if (order_qty == 0) [[unlikely]]
        { // unlikely
            LOG_FILE("3LEGBIDDING", "Leg1 OrderQty is zero");
            LOG_LIVE("3LEGBIDDING", "Leg1 OrderQty is zero");
            std::cerr << "Leg1 OrderQty is 0 for pf id:" << p.portfolio_id << "\n";
            p.is_iter_over = true;
            return true;
        }

        // Place leg 1 order
        Leg leg1{
            p.legs[0].symbol_token,
            leg1_price,
            order_qty,
            p.legs[0].side};

        LOG_FILE("3LEGBIDDING", "Sending placement of single order");

        if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg1_order_id, OrderType::Bidding, leg1, exe_time, true))
        {
            order_data.state = ThreeLegBiddingState::LEG1_PENDING;
            order_data.leg1_timer_start = current_time;
            order_data.current_cycle_qty = order_qty;
            order_data.leg1_price = leg1_price;
            order_data.leg1_pending_qty = order_qty;
            order_data.leg1_filled_qty = 0;
            order_data.last_market_snapshot = s;
            order_data.leg1_ack = false;
            LOG_FILE("3LEGBIDDING", "sended placement of single order" + std::to_string(order_data.leg1_order_id));
            LOG_FILE("3LEGBIDDING", "Updated pending of single new order" + std::to_string(order_data.leg1_pending_qty));
            p.is_iter_over = false;
            order_data.new_max++;

            log.msg_type = StrategyState::NewOrder;
            log.pf_id = p.portfolio_id;
            log.oms_order_id = order_data.leg1_order_id;
            log.token = p.legs[0].symbol_token;
            log.price = leg1_price;
            log.qty = order_qty;
            log.side = p.legs[0].side;
            log.current_spread = spread;
            log.given_spread = p.params.three_leg_bidding.spread;
            log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
            log.market_snapshot = s;

            UltraLog::strategy(log);
            return true;
        }

        LOG_FILE("3LEGBIDDING", "sended placement fail of single order" + std::to_string(order_data.leg1_order_id));
        LOG_LIVE("3LEGBIDDING", "sended placement fail of single order" + std::to_string(order_data.leg1_order_id));

        return true; // this will return true when order limit reaches
    }

    static bool handleLeg1PendingState(Portfolio &p,
                                       const StrategyMarketSnapshot &s,
                                       OrderManager &o,

                                       const auto &params,
                                       auto &order_data,
                                       uint64_t current_time,
                                       uint32_t fut_bid, uint32_t fut_ask,
                                       uint32_t call_bid, uint32_t call_ask,
                                       uint32_t put_bid, uint32_t put_ask,
                                       uint32_t strike, const unsigned long long exe_time) noexcept
    {
        StrategyDataLog log;
        LOG_FILE("3LEGBIDDING", "IN LEG1 PENDING");
        if (!order_data.leg1_ack || (order_data.leg1_cancel_request)) [[unlikely]]
        {
            return true;
        }

        bool is_conversion = order_data.is_conversion;
        p.updated_tick = false;
        // Check timer for leg 1 modification/cancellation
        const uint64_t elapsed = current_time - order_data.leg1_timer_start;
        int spread = INT_MAX;

        if (!p.stop_requested)
        {

            // Check if opportunity still exists
            bool opportunity_exists = params.is_opportunity & checkSpreadOpportunity(p,
                                                                                     is_conversion ? fut_ask : fut_bid,
                                                                                     is_conversion ? call_bid : call_ask,
                                                                                     is_conversion ? put_ask : put_bid,
                                                                                     strike, is_conversion, params, spread);
            LOG_FILE("3LEGBIDDING", "IN Elapsed time");
            LOG_FILE("3LEGBIDDING", "opportunity_exists:" + std::to_string(opportunity_exists));

            if (!opportunity_exists && params.is_opportunity)
            {
                LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel trying, order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id));

                if (order_data.leg1_order_id != 0)
                {
                    LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel");

                    bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);

                    if (is_cancelled)
                    {
                        order_data.leg1_cancel_request = true;
                        log.msg_type = StrategyState::CancelOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.current_spread = spread;
                        log.given_spread = p.params.three_leg_bidding.spread;
                        log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                        log.market_snapshot = s;

                        UltraLog::strategy(log);
                        LOG_FILE("3LEGBIDDING", "Cancel request sended: " + std::to_string(order_data.leg1_order_id));
                    }
                }

                return true;
            }
            else if (order_data.mod_max > order_data.limit_mod_max)
            {
                LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel trying becASE MOD MAX REACH 25, order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id));

                // Cancel leg 1 order and go back to idle
                if (order_data.leg1_order_id != 0)
                {
                    LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel");

                    bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                    if (is_cancelled)
                    {

                        order_data.leg1_cancel_request = true;

                        log.msg_type = StrategyState::CancelOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.current_spread = spread;
                        log.given_spread = p.params.three_leg_bidding.spread;
                        log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                        log.market_snapshot = s;

                        UltraLog::strategy(log);
                        LOG_FILE("3LEGBIDDING", "Cancel request sended: " + std::to_string(order_data.leg1_order_id));
                    }
                }

                return true;
            }
            else
            {

                uint32_t available_qty = 0;
                uint32_t leg1_new_price = 0;

                const auto &bid_leg = p.legs[0];    // bidding leg
                const auto &cover_leg1 = p.legs[1]; // cover leg 1
                const auto &cover_leg2 = p.legs[2]; // cover leg 2

                // Map legs to market data pointers for fast access
                const StoredMarketDataLatency *bid_data = nullptr;
                const StoredMarketDataLatency *cover_data1 = nullptr;
                const StoredMarketDataLatency *cover_data2 = nullptr;

                auto &legs_data = s.data.three_leg_bidding;
                uint32_t fut_token = 0;

                if (bid_leg.leg_type == LegType::Future)
                {
                    fut_token = bid_leg.symbol_token;
                    bid_data = &legs_data.fut;
                    cover_data1 = &legs_data.call;
                    cover_data2 = &legs_data.put;
                }
                else if (bid_leg.leg_type == LegType::Call)
                {
                    bid_data = &legs_data.call;
                    cover_data1 = &legs_data.fut;
                    cover_data2 = &legs_data.put;
                    fut_token = cover_leg1.leg_type == LegType::Future ? cover_leg1.symbol_token : cover_leg2.symbol_token;
                }
                else
                {
                    bid_data = &legs_data.put;
                    cover_data1 = &legs_data.fut;
                    cover_data2 = &legs_data.call;
                    fut_token = cover_leg1.leg_type == LegType::Future ? cover_leg1.symbol_token : cover_leg2.symbol_token;
                }

                available_qty = std::min(cover_leg1.side == OrderSide::Buy ? cover_data1->asks_qty[0] : cover_data1->bids_qty[0], cover_leg2.side == OrderSide::Buy ? cover_data2->asks_qty[0] : cover_data2->bids_qty[0]);
                leg1_new_price = calculateLeg1Price(bid_leg.leg_type, strike,
                                                    cover_leg1.side == OrderSide::Buy ? cover_data1->asks[0] : cover_data1->bids[0],
                                                    cover_leg2.side == OrderSide::Buy ? cover_data2->asks[0] : cover_data2->bids[0],
                                                    params.con_flag, params, fut_token);

                if (leg1_new_price == 0)
                {
                    LOG_FILE("3LEGBIDDING", "IN LEG1 New price 0");
                    LOG_LIVE("3LEGBIDDING", "IN LEG1 New price 0 leg1 pending");
                    std::cerr << "Leg1 Price is 0 for pf id:" << p.portfolio_id << "\n";

                    return true;
                }

                LOG_FILE("3LEGBIDDING", "Current available qty: " + std::to_string(available_qty));

                uint32_t new_order_qty = std::min({params.sol - order_data.leg1_filled_qty, available_qty, params.max_lots - order_data.traded_qty});

                LOG_FILE("3LEGBIDDING", "Current params.sol qty: " + std::to_string(params.sol));
                LOG_FILE("3LEGBIDDING", "Current params.max_lots - order_data.traded_qty qty: " + std::to_string(params.max_lots - order_data.traded_qty));
                LOG_FILE("3LEGBIDDING", "Current  order_data.leg1_pending_qty - order_data.leg1_filled_qty qty: " + std::to_string(order_data.leg1_pending_qty - order_data.leg1_filled_qty));
                LOG_FILE("3LEGBIDDING", "Current  order_data.leg1_filled_qty qty: " + std::to_string(order_data.leg1_filled_qty));
                LOG_FILE("3LEGBIDDING", "Current new_order_qty qty: " + std::to_string(new_order_qty));

                LOG_FILE("3LEGBIDDING", "IN LEG1 Modifying : leg1_new_price:" + std::to_string(leg1_new_price) + ", order_data.leg1_price: " + std::to_string(order_data.last_leg1_price) + ", new_order_qty: " + std::to_string(new_order_qty) + " , order_data.leg1_pending_qty:" + std::to_string(order_data.leg1_pending_qty) + ", order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id) + " , Last quantity leg1 : " + std::to_string(order_data.last_qty_leg1));

                // Only modify if price or quantity changed
                if ((leg1_new_price != order_data.last_leg1_price || new_order_qty != order_data.last_qty_leg1) && order_data.leg1_order_id != 0)
                {
                    LOG_FILE("3LEGBIDDING", "IN LEG1 sure Modifying***********************************************");

                    Leg leg;
                    leg = {p.legs[0].symbol_token, leg1_new_price, order_data.leg1_filled_qty + new_order_qty, p.legs[0].side};
                    LOG_FILE("3LEGBIDDING", "Modify  order because im in handle pending leg1");

                    if (o.sendModifyPlacement(p.portfolio_id, order_data.leg1_order_id, leg, exe_time))
                    {
                        order_data.leg1_price = leg1_new_price;
                        order_data.leg1_pending_qty = new_order_qty;
                        order_data.mod_max++;
                        order_data.last_market_snapshot = s;
                        order_data.leg1_ack = false;
                        LOG_FILE("3LEGBIDDING", "Updated pending of single modify order" + std::to_string(order_data.leg1_pending_qty));

                        log.msg_type = StrategyState::ModifyOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.token = p.legs[0].symbol_token;
                        log.price = leg1_new_price;
                        log.qty = order_data.leg1_filled_qty + new_order_qty;
                        log.side = p.legs[0].side;
                        log.current_spread = spread;
                        log.given_spread = p.params.three_leg_bidding.spread;
                        log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                        log.market_snapshot = s;

                        UltraLog::strategy(log);
                    }
                }
                else
                {
                    LOG_FILE("3LEGBIDDING", "IN LEG1 sure Not Modifying***********************************************");
                }
                order_data.leg1_timer_start = current_time; // Reset timer
            }
        }
        else
        {
            LOG_FILE("3LEGBIDDING", "IN LEG1 Cance in elsel");
            // Cancel leg 1 order and go back to idle
            if (order_data.leg1_order_id != 0)
            {
                LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel");

                // o.cancelOrder(order_data.leg1_order_id);
                bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                if (is_cancelled)
                {

                    order_data.leg1_cancel_request = true;

                    log.msg_type = StrategyState::CancelOrder;
                    log.pf_id = p.portfolio_id;
                    log.oms_order_id = order_data.leg1_order_id;
                    log.current_spread = spread;
                    log.given_spread = p.params.three_leg_bidding.spread;
                    log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                    log.market_snapshot = s;
                    UltraLog::strategy(log);
                    LOG_FILE("3LEGBIDDING", "Cancel Request send as stopped by user");
                }
            }

            return true;
        }

        LOG_FILE("3LEGBIDDING", "Returning true");

        return true;
    }

    // Modified handleLeg1PartialFilledState
    static bool handleLeg1PartialFilledState(Portfolio &p,
                                             const StrategyMarketSnapshot &s,
                                             OrderManager &o,

                                             const auto &params,
                                             auto &order_data,
                                             uint64_t current_time,
                                             uint32_t fut_bid, uint32_t fut_ask,
                                             uint32_t call_bid, uint32_t call_ask,
                                             uint32_t put_bid, uint32_t put_ask,
                                             uint32_t strike, const unsigned long long exe_time) noexcept
    {
        StrategyDataLog log;
        LOG_FILE("3LEGBIDDING", "IN LEG1 Partial FILL");

        const uint32_t partial_filled_qty = order_data.leg1_filled_qty; // 750

        // Handle case where leg2 order was filled
        if (order_data.leg2_filled)
        {
            LOG_FILE("3LEGBIDDING", "Reseting leg2 stats");
            // Reset leg2 state to allow new orders for uncovered quantity
            order_data.leg2_actual_fill_qty += order_data.leg2_filled_qty;
            order_data.leg2_order_id = 0;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_filled = false;
            order_data.leg2_pending_qty = 0;
            order_data.leg2_counter = 0;
            order_data.leg2_depth = 0;
        }

        // Handle case where leg3 order was filled
        if (order_data.leg3_filled)
        {
            LOG_FILE("3LEGBIDDING", "Reseting leg3 stats");
            order_data.leg3_actual_fill_qty += order_data.leg3_filled_qty;

            // Reset leg3 state to allow new orders for uncovered quantity
            order_data.leg3_order_id = 0;
            order_data.leg3_filled = false;
            order_data.leg3_filled_qty = 0;
            order_data.leg3_pending_qty = 0;
            order_data.leg3_counter = 0;
            order_data.leg3_depth = 0;
        }

        // Handle leg2 coverage
        const uint32_t leg2_uncovered = getLeg2UncoveredQty(order_data);
        LOG_FILE("3LEGBIDDING", "Leg 2 uncovered:" + std::to_string(leg2_uncovered) + " leg2 order id:" + std::to_string(order_data.leg2_order_id));
        p.updated_tick = false;
        if (order_data.leg2_order_id == 0 && leg2_uncovered > 0)
        {

            // Place new leg2 order
            Leg leg2;
            OrderSide leg2_side = p.legs[1].side;
            uint32_t price_ask = 0, price_bid = 0;
            switch (p.legs[1].leg_type)
            {
            case LegType::Future:
                price_ask = fut_ask;
                price_bid = fut_bid;
                /* code */
                break;
            case LegType::Call:
                price_ask = call_ask;
                price_bid = call_bid;
                /* code */
                break;
            case LegType::Put:
                price_ask = put_ask;
                price_bid = put_bid;
                /* code */
                break;

            default:
                break;
            }
            uint32_t leg2_price = leg2_side == OrderSide::Buy ? price_ask : price_bid;
            order_data.leg2_price = leg2_price;
            leg2 = {p.legs[1].symbol_token, leg2_price, leg2_uncovered, leg2_side};

            LOG_FILE("3LEGBIDDING", "New  order because im in handle partial filled state leg2");

            if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg2_order_id, OrderType::Bidding, leg2, exe_time, false))
            {
                order_data.legs2_timer_start = current_time;
                order_data.legs2_level = 1;
                order_data.leg2_pending_qty = leg2_uncovered;
                // order_data.last_covered_leg2 = order_data.leg2_covered_qty;
                order_data.leg2_covered_qty += leg2_uncovered;
                order_data.leg2_counter = 0;
                order_data.leg2_depth = 0;
                order_data.last_leg2_qty = leg2_uncovered;
                order_data.leg2_ack = false;
                order_data.leg2_filled = false;
            }
        }
        else if (order_data.leg2_ack == true && !order_data.leg2_filled && order_data.leg2_order_id != 0)
        {

            // Modify existing leg2 order
            const uint32_t old_leg2_qty = order_data.leg2_pending_qty;
            const uint32_t new_leg2_qty = order_data.leg2_pending_qty + leg2_uncovered;
            const uint64_t elapsed_2 = current_time - order_data.legs2_timer_start;
            LOG_FILE("3LEGBIDDING", "Modifying order leg2 order id is: " + std::to_string(order_data.leg2_order_id));
            LOG_FILE("3LEGBIDDING", "Modifying order because im in handle partial fille2: " + std::to_string(new_leg2_qty));
            LOG_FILE("3LEGBIDDING", "Modifying order because im in handle partial fille2: " + std::to_string(new_leg2_qty) + " , last leg qty: " + std::to_string(order_data.last_leg2_qty) + " ,  elapsed 2: " + std::to_string(elapsed_2) + " ," + "params.legs2_timeout_us: " + std::to_string(params.legs2_timeout_us));
            if (new_leg2_qty != order_data.last_leg2_qty || elapsed_2 > params.legs2_timeout_us)
            {

                LOG_FILE("3LEGBIDDING", "Modifying surely order because im in handle partial fille2: here i new leg2 qty " + std::to_string(new_leg2_qty));

                order_data.leg2_pending_qty = new_leg2_qty;
                LOG_FILE("3LEGBIDDING", "Seting last leg2 qty to modify is  " + std::to_string(order_data.leg2_pending_qty));
                bool modified = false;
                handleLeg2Modifications(p, s, o, params, order_data, modified, exe_time);
                if (modified)
                {
                    // order_data.last_covered_leg2 = order_data.leg2_covered_qty;
                    order_data.leg2_covered_qty += leg2_uncovered;
                    order_data.last_qty_leg2 = order_data.last_leg2_qty;
                    // order_data.leg2_pending_qty = new_leg2_qty - order_data.leg2_filled_qty;
                    LOG_FILE("3LEGBIDDING", "Modifed changing everything " + std::to_string(order_data.leg2_pending_qty));
                    order_data.legs2_timer_start = current_time;
                    order_data.last_leg2_qty = new_leg2_qty;
                }
                else
                {
                    order_data.leg2_pending_qty = old_leg2_qty;
                }
            }
        }
        // Handle leg3 coverage
        const uint32_t leg3_uncovered = getLeg3UncoveredQty(order_data);
        LOG_FILE("3LEGBIDDING", "Leg 3 uncovered:" + std::to_string(leg3_uncovered) + " leg 3 order id :" + std::to_string(order_data.leg3_order_id));

        p.updated_tick = false;
        if (order_data.leg3_order_id == 0 && leg3_uncovered > 0)
        {

            Leg leg3;
            OrderSide leg3_side = p.legs[2].side;
            uint32_t price_ask = 0, price_bid = 0;
            switch (p.legs[2].leg_type)
            {
            case LegType::Future:
                price_ask = fut_ask;
                price_bid = fut_bid;
                /* code */
                break;
            case LegType::Call:
                price_ask = call_ask;
                price_bid = call_bid;
                /* code */
                break;
            case LegType::Put:
                price_ask = put_ask;
                price_bid = put_bid;
                /* code */
                break;

            default:
                break;
            }
            uint32_t leg3_price = leg3_side == OrderSide::Buy ? price_ask : price_bid;
            order_data.leg3_price = leg3_price;
            leg3 = {p.legs[2].symbol_token, leg3_price, leg3_uncovered, leg3_side};

            LOG_FILE("3LEGBIDDING", "New  order because im in handle partial filled state leg3");

            if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg3_order_id, OrderType::Bidding, leg3, exe_time, false))
            {
                order_data.legs3_timer_start = current_time;
                order_data.legs3_level = 1;
                // order_data.last_covered_leg3 = order_data.leg3_covered_qty;
                order_data.leg3_pending_qty = leg3_uncovered;
                order_data.leg3_covered_qty += leg3_uncovered;
                order_data.leg3_counter = 0;
                order_data.leg3_depth = 0;
                order_data.last_leg3_qty = leg3_uncovered;
                order_data.leg3_ack = false;
                order_data.leg3_filled = false;
            }
        }
        else if (order_data.leg3_ack == true && !order_data.leg3_filled && order_data.leg3_order_id != 0)
        {
            // Modify existing leg3 order
            LOG_FILE("3LEGBIDDING", "old 3 pending qty:" + std::to_string(order_data.leg3_pending_qty));
            const uint32_t old_leg3_qty = order_data.leg3_pending_qty;
            const uint32_t new_leg3_qty = order_data.leg3_pending_qty + leg3_uncovered;
            const uint64_t elapsed_3 = current_time - order_data.legs3_timer_start;
            if ((new_leg3_qty != order_data.last_leg3_qty) || elapsed_3 > params.legs3_timeout_us)
            {
                order_data.leg3_pending_qty = new_leg3_qty;
                LOG_FILE("3LEGBIDDING", "Seting pending leg3 qty to modify is  " + std::to_string(order_data.leg3_pending_qty));

                bool modified = false;
                handleLeg3Modifications(p, s, o, params, order_data, modified, exe_time);
                if (modified)
                {
                    // order_data.last_covered_leg3 = order_data.leg3_covered_qty;
                    order_data.leg3_covered_qty += leg3_uncovered;
                    LOG_FILE("3LEGBIDDING", "Seting pending leg3 qty to modify because of filled  " + std::to_string(order_data.leg3_pending_qty) + ", filled leg3 is this :" + std::to_string(order_data.leg3_filled_qty));

                    order_data.last_qty_leg3 = order_data.last_leg3_qty;
                    order_data.legs3_timer_start = current_time;
                    order_data.last_leg3_qty = new_leg3_qty;
                    LOG_FILE("3LEGBIDDING", "Modifed changing everything " + std::to_string(order_data.leg3_pending_qty));
                }
                else
                {
                    order_data.leg3_pending_qty = old_leg3_qty;
                }
            }
        }

        // Reset partial fill flag
        order_data.leg1_partial_filled = false;

        if (!order_data.leg1_ack || (order_data.leg1_cancel_request)) [[unlikely]]
        {
            return true;
        }
        bool is_conversion = order_data.is_conversion;
        p.updated_tick = false;

        if (p.stop_requested == false)
        {
            int spread = INT_MAX;
            // Check if opportunity still exists
            bool opportunity_exists = params.is_opportunity & checkSpreadOpportunity(p,
                                                                                     is_conversion ? fut_ask : fut_bid,
                                                                                     is_conversion ? call_bid : call_ask,
                                                                                     is_conversion ? put_ask : put_bid,
                                                                                     strike, is_conversion, params, spread);

            if (!opportunity_exists)
            {
                if (order_data.leg1_order_id != 0)
                {

                    bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                    if (is_cancelled)
                    {

                        order_data.leg1_cancel_request = true;

                        log.msg_type = StrategyState::CancelOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.current_spread = spread;
                        log.given_spread = p.params.three_leg_bidding.spread;
                        log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                        log.market_snapshot = s;
                        UltraLog::strategy(log);
                        LOG_FILE("3LEGBIDDING", "Cancel request sended: " + std::to_string(order_data.leg1_order_id));
                    }
                    return false;
                }
                else
                {
                    LOG_FILE("3legbidding", "Order id not exist current is: " + std::to_string(order_data.leg1_order_id));
                }
                return true;
            }
            else if (order_data.mod_max > order_data.limit_mod_max)
            {
                LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel trying becASE MOD MAX REACH 25, order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id));

                if (order_data.leg1_order_id != 0)
                {
                    LOG_FILE("3LEGBIDDING", "IN LEG1 Cancel");

                    // o.cancelOrder(order_data.leg1_order_id);
                    bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                    if (is_cancelled)
                    {

                        order_data.leg1_cancel_request = true;

                        log.msg_type = StrategyState::CancelOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.current_spread = spread;
                        log.given_spread = p.params.three_leg_bidding.spread;
                        log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                        log.market_snapshot = s;
                        UltraLog::strategy(log);
                        LOG_FILE("3LEGBIDDING", "Cancel request sended: " + std::to_string(order_data.leg1_order_id));
                    }
                }

                return true;
            }
            else if (order_data.leg1_filled_qty <= params.max_lots)
            {

                uint32_t available_qty = 0;
                uint32_t leg1_new_price = 0;

                const auto &bid_leg = p.legs[0];    // bidding leg
                const auto &cover_leg1 = p.legs[1]; // cover leg 1
                const auto &cover_leg2 = p.legs[2]; // cover leg 2
                uint32_t fut_token = 0;

                // Map legs to market data pointers for fast access
                const StoredMarketDataLatency *bid_data = nullptr;
                const StoredMarketDataLatency *cover_data1 = nullptr;
                const StoredMarketDataLatency *cover_data2 = nullptr;

                auto &legs_data = s.data.three_leg_bidding;

                if (bid_leg.leg_type == LegType::Future)
                {
                    fut_token = bid_leg.symbol_token;
                    bid_data = &legs_data.fut;
                    cover_data1 = &legs_data.call;
                    cover_data2 = &legs_data.put;
                }
                else if (bid_leg.leg_type == LegType::Call)
                {
                    bid_data = &legs_data.call;
                    cover_data1 = &legs_data.fut;
                    cover_data2 = &legs_data.put;
                    fut_token = cover_leg1.leg_type == LegType::Future ? cover_leg1.symbol_token : cover_leg2.symbol_token;
                }
                else
                {
                    bid_data = &legs_data.put;
                    cover_data1 = &legs_data.fut;
                    cover_data2 = &legs_data.call;
                    fut_token = cover_leg1.leg_type == LegType::Future ? cover_leg1.symbol_token : cover_leg2.symbol_token;
                }

                available_qty = std::min(cover_leg1.side == OrderSide::Buy ? cover_data1->asks_qty[0] : cover_data1->bids_qty[0], cover_leg2.side == OrderSide::Buy ? cover_data2->asks_qty[0] : cover_data2->bids_qty[0]);
                leg1_new_price = calculateLeg1Price(bid_leg.leg_type, strike,
                                                    cover_leg1.side == OrderSide::Buy ? cover_data1->asks[0] : cover_data1->bids[0],
                                                    cover_leg2.side == OrderSide::Buy ? cover_data2->asks[0] : cover_data2->bids[0],
                                                    true, params, fut_token);

                if (leg1_new_price == 0)
                {
                    return true;
                }

                // FIXED: Calculate uncovered quantity that still needs to be ordered
                // const uint32_t total_covered = getTotalCoveredQty(order_data);
                // const uint32_t total_covered = getTotalCoveredQty(order_data);
                const uint32_t total_covered = getTotalTradedQty(order_data);
                const uint32_t uncovered_qty = order_data.leg1_over_all_fill > total_covered ? order_data.leg1_over_all_fill - total_covered : 0;

                // Calculate new order quantity considering:
                // 1. Remaining SOL capacity (params.sol - order_data.leg1_filled_qty)
                // 2. Available market depth
                // 3. Remaining max lots capacity
                // 4. But don't go below the uncovered quantity that needs hedging
                uint32_t remaining_sol_capacity = params.sol - order_data.leg1_filled_qty;

                if (remaining_sol_capacity <= 0)
                    return true;

                uint32_t new_order_qty = std::min({remaining_sol_capacity,
                                                   available_qty,
                                                   params.max_lots - order_data.traded_qty});
                new_order_qty = std::min(new_order_qty, params.max_lots - order_data.leg1_filled_qty);

                LOG_FILE("3LEGBIDDING", "Leg1 modification calculation:");
                LOG_FILE("3LEGBIDDING", "leg1_filled_qty: " + std::to_string(order_data.leg1_filled_qty));
                LOG_FILE("3LEGBIDDING", "total_covered: " + std::to_string(total_covered));
                LOG_FILE("3LEGBIDDING", "uncovered_qty: " + std::to_string(uncovered_qty));
                LOG_FILE("3LEGBIDDING", "remaining_sol_capacity: " + std::to_string(remaining_sol_capacity));
                LOG_FILE("3LEGBIDDING", "new_order_qty: " + std::to_string(new_order_qty));
                LOG_FILE("3LEGBIDDING", "order_data.leg1_pending_qty: " + std::to_string(order_data.leg1_pending_qty));

                if ((leg1_new_price != order_data.last_leg1_price || (new_order_qty != (order_data.last_qty_leg1 - order_data.leg1_filled_qty) && new_order_qty > 0)) &&
                    order_data.leg1_order_id != 0)
                {
                    LOG_FILE("3LEGBIDDING", "Modifying leg1 order - new_price: " + std::to_string(leg1_new_price) + "old price: " + std::to_string(order_data.leg1_price) +
                                 + " new_order_qty: " + std::to_string(new_order_qty) +
                                 + "order_data.leg1_pending_qty: " + std::to_string(order_data.leg1_pending_qty) +
                                                ", total_qty: " + std::to_string(order_data.leg1_filled_qty + new_order_qty));

                    Leg leg;
                    leg = {p.legs[0].symbol_token, leg1_new_price,
                           order_data.leg1_filled_qty + new_order_qty,
                           p.legs[0].side};

                    if (o.sendModifyPlacement(p.portfolio_id, order_data.leg1_order_id, leg, exe_time))
                    {

                        order_data.leg1_price = leg1_new_price;
                        order_data.leg1_pending_qty = new_order_qty;
                        order_data.last_market_snapshot = s;
                        order_data.mod_max++;
                        order_data.leg1_ack = false;

                        log.msg_type = StrategyState::ModifyOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.token = p.legs[0].symbol_token;
                        log.price = leg1_new_price;
                        log.qty = order_data.leg1_filled_qty + new_order_qty;
                        log.side = p.legs[0].side;
                        log.current_spread = spread;
                        log.given_spread = p.params.three_leg_bidding.spread;
                        log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                        log.market_snapshot = s;

                        LOG_FILE("3LEGBIDDING", "Successfully modified leg1 order");
                    }
                }
                // }

                order_data.leg1_timer_start = current_time; // Reset timer
            }
        }

        else
        {

            // Cancel leg 1 order and go back to idle
            if (order_data.leg1_ack && order_data.leg1_order_id != 0 && (!order_data.leg1_cancel_request))
            {

                bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                if (is_cancelled)
                {

                    order_data.leg1_cancel_request = true;

                    log.msg_type = StrategyState::CancelOrder;
                    log.pf_id = p.portfolio_id;
                    log.oms_order_id = order_data.leg1_order_id;
                    log.current_spread = INT_MAX;
                    log.given_spread = p.params.three_leg_bidding.spread;
                    log.diff = p.params.three_leg_bidding.leg1_spread_threshold;
                    log.market_snapshot = s;
                    UltraLog::strategy(log);
                    LOG_FILE("3LEGBIDDING", "Cancel request sended: " + std::to_string(order_data.leg1_order_id));
                }
            }
        }

        return true;
    }

    // Modified handleLeg1FilledState
    static bool handleLeg1FilledState(Portfolio &p,
                                      const StrategyMarketSnapshot &s,
                                      OrderManager &o,

                                      const auto &params,
                                      auto &order_data,
                                      uint64_t current_time,
                                      uint32_t fut_bid, uint32_t fut_ask,
                                      uint32_t call_bid, uint32_t call_ask,
                                      uint32_t put_bid, uint32_t put_ask,
                                      uint32_t strike, const unsigned long long exe_time) noexcept
    {
        LOG_FILE("3LEGBIDDING", "IN LEG1 FILLED");

        const uint32_t total_filled_qty = order_data.leg1_filled_qty;
        const uint32_t total_covered = getTotalTradedQty(order_data);

        // Check if everything is already covered and filled
        if (total_covered >= order_data.leg1_over_all_fill) [[unlikely]]
        {
            order_data.traded_qty = total_covered;
            p.traded_qty = order_data.traded_qty;
            p.is_data_updated = true;

            // Log the update for debugging
            LOG_FILE("3LEGBIDDING", "LEG1_FILLED: Updated traded_qty by:  Total traded_qty now: " + std::to_string(order_data.traded_qty));

            order_data.state = ThreeLegBiddingState::COMPLETED;
            return false;
        }

        // Handle case where leg2 order was filled
        if (order_data.leg2_filled)
        {
            order_data.leg2_actual_fill_qty += order_data.leg2_filled_qty;

            LOG_FILE("3LEGBIDDING", "Reseting things for leg2");
            // Reset leg2 state to allow new orders for uncovered quantity
            order_data.leg2_order_id = 0;
            order_data.leg2_filled = false;
            order_data.leg2_pending_qty = 0;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_counter = 0;
            order_data.leg2_depth = 0;
        }

        // Handle case where leg3 order was filled
        if (order_data.leg3_filled)
        {
            LOG_FILE("3LEGBIDDING", "Reseting things for leg3");
            order_data.leg3_actual_fill_qty += order_data.leg3_filled_qty;

            // Reset leg3 state to allow new orders for uncovered quantity
            order_data.leg3_order_id = 0;
            order_data.leg3_filled = false;
            order_data.leg3_pending_qty = 0;
            order_data.leg3_filled_qty = 0;
            order_data.leg3_counter = 0;
            order_data.leg3_depth = 0;
        }

        // Handle any remaining uncovered quantities (same logic as partial fill)
        const uint32_t leg2_uncovered = getLeg2UncoveredQty(order_data);
        const uint32_t leg3_uncovered = getLeg3UncoveredQty(order_data);
        LOG_FILE("3LEGBIDDING", "Leg 2 uncovered:" + std::to_string(leg2_uncovered) + " leg2 order id:" + std::to_string(order_data.leg2_order_id));
        LOG_FILE("3LEGBIDDING", "Leg 3 uncovered:" + std::to_string(leg3_uncovered) + " leg3 order id:" + std::to_string(order_data.leg3_order_id));

        p.updated_tick = false;
        if (order_data.leg2_order_id == 0 && leg2_uncovered > 0)
        {

            Leg leg2;
            OrderSide leg2_side = p.legs[1].side;
            uint32_t price_ask = 0, price_bid = 0;
            switch (p.legs[1].leg_type)
            {
            case LegType::Future:
                price_ask = fut_ask;
                price_bid = fut_bid;
                /* code */
                break;
            case LegType::Call:
                price_ask = call_ask;
                price_bid = call_bid;
                /* code */
                break;
            case LegType::Put:
                price_ask = put_ask;
                price_bid = put_bid;
                /* code */
                break;

            default:
                break;
            }
            uint32_t leg2_price = leg2_side == OrderSide::Buy ? price_ask : price_bid;
            order_data.leg2_price = leg2_price;
            leg2 = {p.legs[1].symbol_token, leg2_price, leg2_uncovered, leg2_side};

            LOG_FILE("3LEGBIDDING", "New  order because im in handle filled state of leg1  placing leg2");

            if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg2_order_id, OrderType::Bidding, leg2, exe_time, false))
            {
                order_data.legs2_timer_start = current_time;
                order_data.legs2_level = 1;
                order_data.leg2_pending_qty = leg2_uncovered;
                order_data.leg2_covered_qty += leg2_uncovered;
                order_data.leg2_counter = 0;
                order_data.leg2_depth = 0;
                order_data.last_leg2_qty = leg2_uncovered;
                order_data.leg2_ack = false;
            }
        }
        else if (order_data.leg2_ack == true && !order_data.leg2_filled && order_data.leg2_order_id != 0)
        {

            // Modify existing leg2 order
            const uint32_t old_leg2_qty = order_data.leg2_pending_qty;
            const uint32_t new_leg2_qty = order_data.leg2_pending_qty + leg2_uncovered;
            const uint64_t elapsed_2 = current_time - order_data.legs2_timer_start;
            LOG_FILE("3LEGBIDDING", "Modifying order leg2 order id is: " + std::to_string(order_data.leg2_order_id));
            LOG_FILE("3LEGBIDDING", "Modifying order because im in handle partial fille2: " + std::to_string(new_leg2_qty));
            LOG_FILE("3LEGBIDDING", "Modifying order because im in handle partial fille2: " + std::to_string(new_leg2_qty) + " , last leg qty: " + std::to_string(order_data.last_leg2_qty) + " ,  elapsed 2: " + std::to_string(elapsed_2) + " ," + "params.legs2_timeout_us: " + std::to_string(params.legs2_timeout_us));
            if (new_leg2_qty != order_data.last_leg2_qty || elapsed_2 > params.legs2_timeout_us)
            {

                LOG_FILE("3LEGBIDDING", "Modifying surely order because im in handle partial fille2: here i new leg2 qty " + std::to_string(new_leg2_qty));

                order_data.leg2_pending_qty = new_leg2_qty;
                LOG_FILE("3LEGBIDDING", "Seting last leg2 qty to modify is  " + std::to_string(order_data.leg2_pending_qty));
                bool modified = false;
                handleLeg2Modifications(p, s, o, params, order_data, modified, exe_time);
                if (modified)
                {
                    // order_data.last_covered_leg2 = order_data.leg2_covered_qty;
                    order_data.leg2_covered_qty += leg2_uncovered;
                    order_data.last_qty_leg2 = order_data.last_leg2_qty;
                    // order_data.leg2_pending_qty = new_leg2_qty - order_data.leg2_filled_qty;
                    LOG_FILE("3LEGBIDDING", "Modifed changing everything " + std::to_string(order_data.leg2_pending_qty));
                    order_data.legs2_timer_start = current_time;
                    order_data.last_leg2_qty = new_leg2_qty;
                }
                else
                {
                    order_data.leg2_pending_qty = old_leg2_qty;
                }
            }
        }

        if (order_data.leg3_order_id == 0 && leg3_uncovered > 0)
        {

            Leg leg3;
            OrderSide leg3_side = p.legs[2].side;
            uint32_t price_ask = 0, price_bid = 0;
            switch (p.legs[2].leg_type)
            {
            case LegType::Future:
                price_ask = fut_ask;
                price_bid = fut_bid;
                /* code */
                break;
            case LegType::Call:
                price_ask = call_ask;
                price_bid = call_bid;
                /* code */
                break;
            case LegType::Put:
                price_ask = put_ask;
                price_bid = put_bid;
                /* code */
                break;

            default:
                break;
            }
            uint32_t leg3_price = leg3_side == OrderSide::Buy ? price_ask : price_bid;
            order_data.leg3_price = leg3_price;
            leg3 = {p.legs[2].symbol_token, leg3_price, leg3_uncovered, leg3_side};

            LOG_FILE("3LEGBIDDING", "New order because leg3 alrwad done handle filled state");

            if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg3_order_id, OrderType::Bidding, leg3, exe_time, false))
            {
                order_data.legs3_timer_start = current_time;
                order_data.legs3_level = 1;
                order_data.leg3_pending_qty = leg3_uncovered;
                order_data.leg3_covered_qty += leg3_uncovered;
                order_data.leg3_counter = 0;
                order_data.leg3_depth = 0;
                order_data.last_leg3_qty = leg3_uncovered;
                order_data.leg3_ack = false;
            }
        }
        else if (order_data.leg3_ack == true && !order_data.leg3_filled && order_data.leg3_order_id != 0)
        {

            // Modify existing leg3 order
            LOG_FILE("3LEGBIDDING", "old 3 pending qty:" + std::to_string(order_data.leg3_pending_qty));
            const uint32_t old_leg3_qty = order_data.leg3_pending_qty;
            const uint32_t new_leg3_qty = order_data.leg3_pending_qty + leg3_uncovered;
            const uint64_t elapsed_3 = current_time - order_data.legs3_timer_start;
            if ((new_leg3_qty != order_data.last_leg3_qty) || elapsed_3 > params.legs3_timeout_us)
            {
                order_data.leg3_pending_qty = new_leg3_qty;
                LOG_FILE("3LEGBIDDING", "Seting pending leg3 qty to modify is  " + std::to_string(order_data.leg3_pending_qty));

                bool modified = false;
                handleLeg3Modifications(p, s, o, params, order_data, modified, exe_time);
                if (modified)
                {

                    // order_data.last_covered_leg3 = order_data.leg3_covered_qty;
                    order_data.leg3_covered_qty += leg3_uncovered;
                    order_data.last_qty_leg3 = order_data.last_leg3_qty;
                    // order_data.leg3_pending_qty = new_leg3_qty - order_data.leg3_filled_qty;
                    LOG_FILE("3LEGBIDDING", "Seting pending leg3 qty to modify because of filled  " + std::to_string(order_data.leg3_pending_qty) + ", filled leg3 is this :" + std::to_string(order_data.leg3_filled_qty));

                    order_data.legs3_timer_start = current_time;
                    order_data.last_leg3_qty = new_leg3_qty;
                    LOG_FILE("3LEGBIDDING", "Modifed changing everything " + std::to_string(order_data.leg3_pending_qty));
                }
                else
                {
                    order_data.leg3_pending_qty = old_leg3_qty;
                }
            }
        }

        // Transition to monitoring state
        if (order_data.leg2_ack == true && order_data.leg3_ack == true && (leg3_uncovered <= 0 && leg2_uncovered <= 0))
        {
            order_data.state = ThreeLegBiddingState::LEGS23_PENDING; // *****************chnaged
        }
        p.updated_tick = false;

        return true;
    }

    // Modified handleLegs23PendingState to handle separate leg timers
    static bool handleLegs23PendingState(Portfolio &p,
                                         const StrategyMarketSnapshot &s,
                                         OrderManager &o,

                                         const auto &params,
                                         auto &order_data,
                                         uint64_t current_time,
                                         uint32_t fut_bid, uint32_t fut_ask,
                                         uint32_t call_bid, uint32_t call_ask,
                                         uint32_t put_bid, uint32_t put_ask,
                                         uint32_t strike, const unsigned long long exe_time) noexcept
    {
        LOG_FILE("3LEGBIDDING", "IN LEG2 &3 PENDING STATE");

        // Check for completion
        const uint32_t total_covered = getTotalTradedQty(order_data);
        const uint32_t leg2_uncovered = getLeg2UncoveredQty(order_data);
        const uint32_t leg3_uncovered = getLeg3UncoveredQty(order_data);

        // FIXED: Check for completion with proper quantity tracking
        LOG_FILE("3LEGBIDDING", "Important information :total_covered:" + std::to_string(total_covered) + " ,order_data.leg1_filled_qty:" + std::to_string(order_data.leg1_filled_qty));
        LOG_FILE("3LEGBIDDING", "Important information 2nd ," + std::to_string(params.legs2_timeout_us) + "," + std::to_string(order_data.leg2_filled) + "," + std::to_string(order_data.leg2_order_id));
        LOG_FILE("3LEGBIDDING", "Important information 3rd ," + std::to_string(params.legs3_timeout_us) + "," + std::to_string(order_data.leg3_filled) + "," + std::to_string(order_data.leg3_order_id));

        LOG_FILE("3LEGBIDDING", "Leg2 and leg3 filled qty1:" + std::to_string(order_data.leg2_filled_qty) + " , " + std::to_string(order_data.leg3_filled_qty));
        // const uint32_t total_covered = getTotalCoveredQty(order_data);
        if (total_covered >= order_data.leg1_over_all_fill)
        {
            LOG_FILE("3LEGBIDDING", "Leg2 and leg3 filled qty2:" + std::to_string(order_data.leg2_over_all_fill) + " , " + std::to_string(order_data.leg3_over_all_fill));
            // Update traded quantity
            order_data.traded_qty = total_covered;
            p.traded_qty = order_data.traded_qty; // IMPORTANT AS TRADED UPDATED WE HAVE TO UPDATE TO FRONTEND
            p.is_data_updated = true;

            // Log the update
            LOG_FILE("3LEGBIDDING", "LEGS23_PENDING: Updated traded_qty by:  Total traded_qty now: " + std::to_string(order_data.traded_qty));

            order_data.state = ThreeLegBiddingState::COMPLETED;
            return false;
        }

        // Handle leg2 timer and modifications
        const uint64_t elapsed_2 = current_time - order_data.legs2_timer_start;
        LOG_FILE("3LEGBIDDING", "Important information 2nd" + std::to_string(elapsed_2) + " ," + std::to_string(params.legs2_timeout_us) + "," + std::to_string(order_data.leg2_filled) + "," + std::to_string(order_data.leg2_order_id));

        // LOG_FILE("3LEGBIDDING","elapsed: " + std::to_string(elapsed_2));
        if (order_data.leg2_ack == true && elapsed_2 > params.legs2_timeout_us && !order_data.leg2_filled && order_data.leg2_order_id != 0)
        {
            bool modified = false;
            LOG_FILE("3LEGBIDDING", "elapsed so going Modify fun 2nd leg" + std::to_string(elapsed_2) + " ," + std::to_string(params.legs2_timeout_us));
            handleLeg2Modifications(p, s, o, params, order_data, modified, exe_time);
            // order_data.leg2_level2 = true;
            if (modified)
            {

                order_data.legs2_timer_start = current_time;
            }
        }
        else if (order_data.leg2_ack == true && order_data.leg2_order_id == 0 && leg2_uncovered > 0)
        {

            Leg leg2;
            OrderSide leg2_side = p.legs[1].side;
            uint32_t price_ask = 0, price_bid = 0;
            switch (p.legs[1].leg_type)
            {
            case LegType::Future:
                price_ask = fut_ask;
                price_bid = fut_bid;
                /* code */
                break;
            case LegType::Call:
                price_ask = call_ask;
                price_bid = call_bid;
                /* code */
                break;
            case LegType::Put:
                price_ask = put_ask;
                price_bid = put_bid;
                /* code */
                break;

            default:
                break;
            }
            uint32_t leg2_price = leg2_side == OrderSide::Buy ? price_ask : price_bid;
            order_data.leg2_price = leg2_price;
            leg2 = {p.legs[1].symbol_token, leg2_price, leg2_uncovered, leg2_side};

            LOG_FILE("3LEGBIDDING", "New  order because im in handle filled state of leg1  placing leg2");

            if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg2_order_id, OrderType::Bidding, leg2, exe_time, false))
            {
                order_data.legs2_timer_start = current_time;
                order_data.legs2_level = 1;
                order_data.leg2_pending_qty = leg2_uncovered;
                order_data.leg2_covered_qty += leg2_uncovered;
                order_data.leg2_counter = 0;
                order_data.leg2_depth = 0;
                order_data.last_leg2_qty = leg2_uncovered;
                order_data.leg2_ack = false;
            }
        }

        // Handle leg3 timer and modifications
        const uint64_t elapsed_3 = current_time - order_data.legs3_timer_start;
        LOG_FILE("3LEGBIDDING", "Important information 3rd" + std::to_string(elapsed_3) + " ," + std::to_string(params.legs3_timeout_us) + "," + std::to_string(order_data.leg3_filled) + "," + std::to_string(order_data.leg3_order_id));

        // LOG_FILE("3LEGBIDDING","elapsed: " + std::to_string(elapsed_3));

        if (order_data.leg3_ack == true && elapsed_3 > params.legs3_timeout_us && !order_data.leg3_filled && order_data.leg3_order_id != 0)
        {
            bool modified = false;
            LOG_FILE("3LEGBIDDING", "elapsed so going Modify fun 3rd leg" + std::to_string(elapsed_3) + " ," + std::to_string(params.legs3_timeout_us));
            handleLeg3Modifications(p, s, o, params, order_data, modified, exe_time);
            // order_data.leg3_level2 = true;
            if (modified)
            {

                order_data.legs3_timer_start = current_time;
            }
        }
        else if (order_data.leg3_ack == true && order_data.leg3_order_id == 0 && leg3_uncovered > 0)
        {

            Leg leg3;
            OrderSide leg3_side = p.legs[2].side;
            uint32_t price_ask = 0, price_bid = 0;
            switch (p.legs[2].leg_type)
            {
            case LegType::Future:
                price_ask = fut_ask;
                price_bid = fut_bid;
                /* code */
                break;
            case LegType::Call:
                price_ask = call_ask;
                price_bid = call_bid;
                /* code */
                break;
            case LegType::Put:
                price_ask = put_ask;
                price_bid = put_bid;
                /* code */
                break;

            default:
                break;
            }
            uint32_t leg3_price = leg3_side == OrderSide::Buy ? price_ask : price_bid;
            order_data.leg3_price = leg3_price;
            leg3 = {p.legs[2].symbol_token, leg3_price, leg3_uncovered, leg3_side};

            LOG_FILE("3LEGBIDDING", "New order because leg3 alrwad done handle filled state");

            if (o.sendSingleLegOrder(p.portfolio_id, order_data.leg3_order_id, OrderType::Bidding, leg3, exe_time, false))
            {
                order_data.legs3_timer_start = current_time;
                order_data.legs3_level = 1;
                order_data.leg3_pending_qty = leg3_uncovered;
                order_data.leg3_covered_qty += leg3_uncovered;
                order_data.leg3_counter = 0;
                order_data.leg3_depth = 0;
                order_data.last_leg3_qty = leg3_uncovered;
                order_data.leg3_ack = false;
            }
        }

        return true;
    }

    // Modified leg modification functions to use separate quantities
    static void handleLeg2Modifications(Portfolio &p,
                                        const StrategyMarketSnapshot &s,
                                        OrderManager &o,
                                        const auto &params,
                                        auto &order_data, bool &modified, const unsigned long long exe_time) noexcept
    {
        LOG_FILE("3legBid", "Enetered 2nd Modification");
        p.updated_tick = false;
        order_data.leg2_counter++;

        if (order_data.leg2_counter >= 4)
        {
            order_data.leg2_depth = 1;
            order_data.leg2_counter = 0;
        }

        uint32_t current_leg2_price = 0;
        const StoredMarketDataLatency *leg2_data = (p.legs[1].leg_type == LegType::Future)
                                                       ? &s.data.three_leg_bidding.fut
                                                   : (p.legs[1].leg_type == LegType::Call)
                                                       ? &s.data.three_leg_bidding.call
                                                       : &s.data.three_leg_bidding.put;

        current_leg2_price = (p.legs[1].side==Side::Buy)
                                 ? ((order_data.leg2_depth == 0) ? leg2_data->asks[0] : leg2_data->asks[1])
                                 : ((order_data.leg2_depth == 0) ? leg2_data->bids[0] : leg2_data->bids[1]);

        if (current_leg2_price == 0 && order_data.leg2_depth == 1)
        {
            order_data.leg2_depth = 0;
            current_leg2_price = p.legs[1].side== Side::Sell ? leg2_data->bids[0] : leg2_data->asks[0];
        }

        LOG_FILE("3LEGBIDDING", "current_leg2_price:" + std::to_string(current_leg2_price) + ",order_data.last_leg2_price:" + std::to_string(order_data.last_leg2_price) + ",order_data.leg2_pending_qty :" + std::to_string(order_data.leg2_pending_qty) + ",order_data.last_leg2_qty:" + std::to_string(order_data.last_leg2_qty) + " oms id for this is :" + std::to_string(order_data.leg2_order_id));

        if (current_leg2_price > 0 &&
            (current_leg2_price != order_data.last_leg2_price || order_data.leg2_filled_qty + order_data.leg2_pending_qty != order_data.last_leg2_qty))
        {

            LOG_FILE("3LEGBIDDING", "Modifying order because im in handle leg 2 modify fun");

            Leg leg2;
            leg2 = {p.legs[1].symbol_token, current_leg2_price, order_data.leg2_filled_qty + order_data.leg2_pending_qty, p.legs[1].side, 0, order_data.leg2_order_id};

            if (o.sendModifyPlacement(p.portfolio_id, order_data.leg2_order_id, leg2, exe_time, 2))
            {
                order_data.leg2_price = current_leg2_price;
                modified = true;
                order_data.leg2_ack = false;
            }
        }
    }

    static void handleLeg3Modifications(Portfolio &p,
                                        const StrategyMarketSnapshot &s,
                                        OrderManager &o,
                                        const auto &params,
                                        auto &order_data, bool &modified, const unsigned long long exe_time) noexcept
    {
        p.updated_tick = false;
        order_data.leg3_counter++;

        if (order_data.leg3_counter >= 4)
        {
            order_data.leg3_depth = 1;
            order_data.leg3_counter = 0;
        }

        uint32_t current_leg3_price = 0;
        const StoredMarketDataLatency *leg3_data = (p.legs[2].leg_type == LegType::Future)
                                                       ? &s.data.three_leg_bidding.fut
                                                   : (p.legs[2].leg_type == LegType::Call)
                                                       ? &s.data.three_leg_bidding.call
                                                       : &s.data.three_leg_bidding.put;

        current_leg3_price = (p.legs[2].side == Side::Buy)
                                 ? ((order_data.leg3_depth == 0) ? leg3_data->asks[0] : leg3_data->asks[1])
                                 : ((order_data.leg3_depth == 0) ? leg3_data->bids[0] : leg3_data->bids[1]);

        if (current_leg3_price == 0 && order_data.leg3_depth == 1)
        {
            order_data.leg3_depth = 0;
            current_leg3_price = p.legs[2].side == Side::Buy ? leg3_data->asks[0] : leg3_data->bids[0];
        }

        LOG_FILE("3LEGBIDDING", "current_leg3_price:" + std::to_string(current_leg3_price) + ",order_data.last_leg3_price:" + std::to_string(order_data.last_leg3_price) + ",order_data.leg3_pending_qty :" + std::to_string(order_data.leg3_pending_qty) + ",order_data.last_leg3_qty:" + std::to_string(order_data.last_leg3_qty) + " oms id for this is :" + std::to_string(order_data.leg3_order_id));

        if (current_leg3_price > 0 &&
            (current_leg3_price != order_data.last_leg3_price || order_data.leg3_filled_qty + order_data.leg3_pending_qty != order_data.last_leg3_qty))
        {

            LOG_FILE("3LEGBIDDING", "Modifying order because im in func og leg3 modify");

            Leg leg3;
            leg3 = {p.legs[2].symbol_token, current_leg3_price, order_data.leg3_filled_qty + order_data.leg3_pending_qty, p.legs[2].side, 0, order_data.leg3_order_id};

            if (o.sendModifyPlacement(p.portfolio_id, order_data.leg3_order_id, leg3, exe_time, 3))
            {
                order_data.leg3_price = current_leg3_price;
                modified = true;
                order_data.leg3_ack = false;
            }
        }
    }

    static bool handleCompletedState(Portfolio &p,
                                     const StrategyMarketSnapshot &s,
                                     OrderManager &o,

                                     const auto &params,
                                     auto &order_data,
                                     uint64_t current_time) noexcept
    {
        p.is_iter_over = true;
        LOG_FILE("3LEGBIDDING", "IN COMPLETE STATE");

        LOG_FILE("3LEGBIDDING", "Completed strategy: traded_qty: " + std::to_string(order_data.traded_qty) +
                                    " max_lots: " + std::to_string(params.max_lots));

        // Check if we can place another cycle
        if (order_data.traded_qty >= params.max_lots)
        {
            LOG_FILE("3LEGBIDDING", "Max lots reached, terminating strategy. traded and maxlots:" + std::to_string(order_data.traded_qty) + "," + std::to_string(params.max_lots));
            LOG_LIVE("3LEGBIDDING", "Max lots reached, terminating strategy. traded and maxlots:" + std::to_string(order_data.traded_qty) + "," + std::to_string(params.max_lots));

            p.is_active = 0;
            p.terminate = 1;
            p.traded_qty = order_data.traded_qty;
            p.is_data_updated = true;
            return true;
        }

        // Reset for next cycle
        resetOrderData(order_data);
        order_data.new_max = 0;
        order_data.state = ThreeLegBiddingState::IDLE;
        return false; // Will start new cycle on next tick
    }

    static bool handleExitState(Portfolio &p,
                                const StrategyMarketSnapshot &s,
                                OrderManager &o,

                                const auto &params,
                                auto &order_data,
                                uint64_t current_time) noexcept
    {
        LOG_FILE("3LEGBID", "Came in handle EXIT");
        p.stop_requested = true;
        if (order_data.leg1_cancel_request || (!order_data.leg1_ack))
        {
            return true;
        }
        if (order_data.leg1_ack && order_data.leg1_order_id != 0)
        {
            bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);

            if (is_cancelled)
            {
                order_data.leg1_cancel_request = true;

                LOG_FILE("3legbidding", "Cancelled the open leg as EXITING");

                return true;
            }
            else
            {
                LOG_FILE("3legbidding", "not able Cancelled the open leg");
            }
        }
        else if (getTotalTradedQty(order_data) < order_data.leg1_over_all_fill) // getTotalCoveredFilledQty(order_data) on place of this use exact filled value of both
        {
            order_data.state = ThreeLegBiddingState::LEG1_FILLED;
            p.is_active = true;
            p.is_iter_over = false;
            return false;
        }
        else
        {
            LOG_FILE("3LEGBIDDING", "non of above case in EXIT");
            order_data.state = ThreeLegBiddingState::COMPLETED;

            return false;
        }

        return true;
    }

    // Modified reset functions
    static void resetOrderData(auto &order_data) noexcept
    {
        order_data.leg1_filled = false;
        order_data.leg1_partial_filled = false;
        order_data.leg2_filled = false;
        order_data.leg3_filled = false;
        order_data.leg1_order_id = 0;
        order_data.leg2_order_id = 0;
        order_data.leg3_order_id = 0;
        order_data.current_cycle_qty = 0;
        order_data.leg1_pending_qty = 0;
        order_data.leg1_filled_qty = 0;
        order_data.leg1_price = 0;

        // Reset separate coverage tracking
        order_data.leg2_covered_qty = 0;
        order_data.leg3_covered_qty = 0;
        order_data.leg2_pending_qty = 0;
        order_data.leg3_pending_qty = 0;
        order_data.leg2_actual_fill_qty = 0;
        order_data.leg3_actual_fill_qty = 0;

        order_data.mod_max = 0;
        resetCoverLegStates(order_data);
    }

    static void resetCoverLegStates(auto &order_data) noexcept
    {
        // Reset leg 2 state
        order_data.leg2_filled = false;
        order_data.leg2_order_id = 0;
        order_data.leg2_filled_qty = 0;
        order_data.leg2_counter = 0;
        order_data.leg2_depth = 0;
        order_data.last_leg2_price = 0;
        order_data.last_leg2_qty = 0;
        order_data.leg2_covered_qty = 0;
        order_data.leg2_pending_qty = 0;

        // Reset leg 3 state
        order_data.leg3_filled = false;
        order_data.leg3_order_id = 0;
        order_data.leg3_filled_qty = 0;
        order_data.leg3_counter = 0;
        order_data.leg3_depth = 0;
        order_data.last_leg3_price = 0;
        order_data.last_leg3_qty = 0;
        order_data.leg3_covered_qty = 0;
        order_data.leg3_pending_qty = 0;
    }

    static void printMarketData(const StoredMarketDataLatency &data, const Portfolio &p)
    {
        LOG_FILE("3LEGBIDDINGLive", "Start Time: " + std::to_string(data.start_time));
        LOG_FILE("3LEGBIDDINGLive", "seqno: " + std::to_string(data.seqno));
        LOG_FILE("3LEGBIDDINGLive", "msg_type: " + std::to_string(data.msg_type));
        LOG_FILE("3LEGBIDDINGLive", "internal_seqno: " + std::to_string(data.internal_seqno));
        LOG_FILE("3LEGBIDDINGLive", "stream_id: " + std::to_string(data.stream_id));
        LOG_FILE("3LEGBIDDINGLive", "last_traded_price: " + std::to_string(data.last_traded_price));

        for (int i = 0; i < 5; ++i)
        {
            LOG_FILE("3LEGBIDDINGLive",
                     "Level " + std::to_string(i + 1) +
                         " | Bid: " + std::to_string(data.bids[i]) +
                         " (" + std::to_string(data.bids_qty[i]) + ")" +
                         " | Ask: " + std::to_string(data.asks[i]) +
                         " (" + std::to_string(data.asks_qty[i]) + ")");
        }
    }
};

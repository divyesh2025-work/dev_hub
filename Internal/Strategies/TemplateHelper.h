#pragma once
#include "TemplateStrategy.h"
#include "ConRevTemplate.h"
#include "Bidding3Template.h"
#include "BoxBidding.h"
#include "BoxIoc.h"

#define CANCEL_MAX_4Leg_BIDDING 1000000
#define MOD_MAX_4Leg_BIDDING 1000000

// Runtime dispatch function using switch (branch predictor friendly)
ALWAYS_INLINE bool executeStrategy(Portfolio &p,
                                   const StrategyMarketSnapshot &s,
                                   OrderManager &o,
                                   const unsigned long long exe_time) noexcept
{
    switch (p.kind)
    {
    case StrategyKind::CONREV_IOC:
        return StrategyExecutor<StrategyKind::CONREV_IOC>::run(p, s, o, exe_time);
    case StrategyKind::CONREV_BID:
        return StrategyExecutor<StrategyKind::CONREV_BID>::run(p, s, o, exe_time);
    case StrategyKind::BOX_1_1_1_1:
    {
        LOG_FILE("BoxBidding", "Box Bidding called on Executor called");
        return StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(p, s, o, exe_time);
    }
    case StrategyKind::BOX_2_1_1:
        return StrategyExecutor<StrategyKind::BOX_2_1_1>::run(p, s, o, exe_time);

    default:
        return false;
    }
}

ALWAYS_INLINE void handleExchangeAck(Portfolio &strat, uint32_t strategy_order_id, uint32_t price, uint32_t qty) noexcept
{
    LOG_FILE("TEMPLATE_HELPER", "Price:" + std::to_string(price) + ",qty:" + std::to_string(qty));

    switch (strat.kind)
    {

    case StrategyKind::CONREV_IOC:
    {
        break;
    }

    case StrategyKind::CONREV_BID:
    {
        auto &order_data = strat.order_data.three_leg_bidding;

        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            order_data.leg1_ack = true;
            order_data.leg1_price = price;
            order_data.last_leg1_price = order_data.leg1_price;
            order_data.last_qty_leg1 = order_data.leg1_pending_qty;
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_ack = true;
            order_data.leg2_price = price;
            order_data.last_leg2_price = order_data.leg2_price;
            order_data.last_covered_leg2 = order_data.leg2_covered_qty;
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.leg3_price = price;
            order_data.last_leg3_price = order_data.leg3_price;
            order_data.last_covered_leg3 = order_data.leg3_covered_qty;
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in exchange ack: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange ack:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange ack:" + std::to_string(strategy_order_id));
        }

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        LOG_FILE("DEBUG", "In Box_1_1_1_1 handleExchangeAck");
        LOG_FILE("TemplateHelper", "price: " + std::to_string(price) + "qty: " + std::to_string(qty));

        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id) + ", leg4 order id:" + std::to_string(order_data.leg4_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            order_data.leg1_ack = true;
            order_data.leg1_price = price;
            order_data.last_leg1_price = price;
            order_data.leg1_pending_qty = qty;
            order_data.last_leg1_qty = qty;
            order_data.leg1_filled = false;
            strat.is_iter_over = false;
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_ack = true;
            order_data.leg2_price = price;
            order_data.last_leg2_price = price;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_pending_qty = qty;
            order_data.last_leg2_qty = qty;
            order_data.leg2_filled = false;
            order_data.last_covered_leg2 = order_data.leg2_covered_qty;
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.leg3_price = price;
            order_data.last_leg3_price = price;
            order_data.leg3_filled_qty = 0;
            order_data.leg3_pending_qty = qty;
            order_data.last_leg3_qty = qty;
            order_data.leg3_filled = false;
            order_data.last_covered_leg3 = order_data.leg3_covered_qty;
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            order_data.leg4_ack = true;
            order_data.leg4_price = price;
            order_data.last_leg4_price = price;
            order_data.leg4_filled_qty = 0;
            order_data.leg4_pending_qty = qty;
            order_data.last_leg4_qty = qty;
            order_data.leg4_filled = false;
            order_data.last_covered_leg4 = order_data.leg4_covered_qty;
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in exchange ack: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange ack: " + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange ack: " + std::to_string(strategy_order_id));
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleNewAck(strat, strategy_order_id);
        break;
    }

    default:
    {

        LOG_LIVE("TEMPLATE_HELPER", "Default case of on Handle Fill");
        break;
    }
    }
}

ALWAYS_INLINE void handleExchangeModifyAck(Portfolio &strat, uint32_t strategy_order_id, uint32_t price, uint32_t qty) noexcept
{
    LOG_FILE("TEMPLATE_HELPER", "Price:" + std::to_string(price) + ",qty:" + std::to_string(qty));

    switch (strat.kind)
    {

    case StrategyKind::CONREV_IOC:
    {
        break;
    }

    case StrategyKind::CONREV_BID:
    {
        auto &order_data = strat.order_data.three_leg_bidding;

        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            order_data.leg1_ack = true;
            order_data.leg1_price = price;
            order_data.last_leg1_price = order_data.leg1_price;
            order_data.last_qty_leg1 = order_data.leg1_pending_qty + order_data.leg1_filled;
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_ack = true;
            order_data.leg2_price = price;
            order_data.last_leg2_price = order_data.leg2_price;
            order_data.leg2_pending_qty = qty-order_data.leg2_filled_qty;
            order_data.last_covered_leg2 = order_data.leg2_covered_qty;
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.leg3_price = price;
            order_data.last_leg3_price = order_data.leg3_price;
            order_data.leg3_pending_qty = qty-order_data.leg3_filled_qty;
            order_data.last_covered_leg3 = order_data.leg3_covered_qty;
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in exchange modify ack: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange modify ack:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange modify ack:" + std::to_string(strategy_order_id));
        }

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id) + ", leg4 order id:" + std::to_string(order_data.leg4_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            order_data.leg1_ack = true;
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_ack = true;
            order_data.last_covered_leg2 = order_data.leg2_covered_qty;
            order_data.last_leg2_qty = qty;
            order_data.last_leg2_price = price;
            order_data.leg2_pending_qty = qty - order_data.leg2_filled_qty;
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.last_covered_leg3 = order_data.leg3_covered_qty;
            order_data.last_leg3_qty = qty;
            order_data.last_leg3_price = price;
            order_data.leg3_pending_qty = qty - order_data.leg3_filled_qty;
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            order_data.leg4_ack = true;
            order_data.last_covered_leg4 = order_data.leg4_covered_qty;
            order_data.last_leg4_qty = qty;
            order_data.last_leg4_price = price;
            order_data.leg4_pending_qty = qty - order_data.leg4_filled_qty;
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in exchange modify ack: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange modify ack:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in exchange modify ack:" + std::to_string(strategy_order_id));
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleModifyAck(strat, strategy_order_id);
        break;
    }

    default:
    {
        LOG_LIVE("TEMPLATE_HELPER", "Default case of on Handle Fill");
        break;
    }
    }
}

ALWAYS_INLINE void handleFill(Portfolio &strat, uint32_t strategy_order_id, uint32_t fill_qty, uint32_t fill_price, int32_t &leg_id, OrderManager &order_manager, unsigned long long exe_time, ska::flat_hash_map<uint32_t, StoredMarketDataLatency> &orderbook) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC:
    {
        auto &order_data = strat.order_data.conrev;

        uint8_t leg = order_data.order_id_to_leg.lookup(strategy_order_id);
        switch (leg)
        {
        case 0:
        {
            leg_id = 1;
            order_data.traded_qty += fill_qty;
            order_data.remain_qty -= fill_qty; // not using right now anywhere
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v1_fut += total_value;
            // order_data.q1_fut += fill_qty;

            LOG_TEST("Leg1 (Future) filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg1 (Future) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* fut */ break;
        }
        case 1:
        {
            leg_id = 2;
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += total_value;
            // order_data.q2_call += fill_qty;

            LOG_TEST("Leg2 (Call) filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* call */ break;
        }

        case 2:
        {
            leg_id = 3;
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += total_value;
            // order_data.q3_put += fill_qty;

            LOG_TEST("Leg3 (Put) filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* put */ break;
        }
        default:
            LOG_FILE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            LOG_LIVE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            break;
        }

        // // Check if all legs are filled
        // if (order_data.q1_fut > 0 && order_data.q1_fut == order_data.q2_call && order_data.q2_call == order_data.q3_put)
        // {
        //     order_data.price_fut = order_data.v1_fut / order_data.q1_fut;
        //     order_data.price_call = order_data.v2_call / order_data.q2_call;
        //     order_data.price_put = order_data.v3_put / order_data.q3_put;

        //     if (order_data.con_flag)
        //     {
        //         LOG_FILE("TEMPLATE_HELPER", "Achived spread 4.1 : " + std::to_string(order_data.strike_price) + " - " + std::to_string(order_data.price_fut) + " + " + std::to_string(order_data.price_call) + "-" + std::to_string(order_data.price_put));

        //         order_data.achieved_spread = order_data.strike_price - order_data.price_fut + order_data.price_call - order_data.price_put;
        //     }
        //     else
        //     {
        //         LOG_FILE("TEMPLATE_HELPER", "Achived spread 4.2 : " + std::to_string(order_data.strike_price) + " + " + std::to_string(order_data.price_fut) + " -" + std::to_string(order_data.price_call) + "+" + std::to_string(order_data.price_put));

        //         order_data.achieved_spread = -order_data.strike_price + order_data.price_fut - order_data.price_call + order_data.price_put;
        //     }
        // }
        strat.achieved_spread = order_data.achieved_spread;
        strat.traded_qty = order_data.traded_qty;
        //

        break;
    }

    case StrategyKind::CONREV_BID:
    {

        auto &order_data = strat.order_data.three_leg_bidding;
        LOG_FILE("TEMPLATE",
                 "For Spread: strategy id: " + std::to_string(strategy_order_id) +
                     ", leg1 order id: " + std::to_string(order_data.leg1_order_id) +
                     ", leg2 order id: " + std::to_string(order_data.leg2_order_id) +
                     ", leg3 order id: " + std::to_string(order_data.leg3_order_id) +
                     ", FUT value: " + std::to_string(order_data.v1_fut) +
                     ", CALL value: " + std::to_string(order_data.v2_call) +
                     ", PUT value: " + std::to_string(order_data.v3_put) +
                     ", FUT qty: " + std::to_string(order_data.q1_fut) +
                     ", CALL qty: " + std::to_string(order_data.q2_call) +
                     ", PUT qty: " + std::to_string(order_data.q3_put));
        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            leg_id = 1;
            // Leg 1 (Future) filled completely
            order_data.leg1_filled = true;
            order_data.leg1_cancel_request = false;
            order_data.leg1_ack = true;
            order_data.leg1_filled_qty += fill_qty; // Complete fill, so set total filled qty
            order_data.leg1_pending_qty = 0;        // No more pending
            order_data.leg1_order_id = 0;
            order_data.leg1_over_all_fill += fill_qty;

            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v1_fut += total_value;
            order_data.q1_fut += fill_qty;
            strat.is_active = true;
            strat.is_iter_over = false;
            order_data.mod_max = 0;
            order_data.new_max = 0;
            order_data.state = ThreeLegBiddingState::LEG1_FILLED;
            const uint32_t fut_token = strat.legs[0].symbol_token;
            const uint32_t call_token = strat.legs[1].symbol_token;
            const uint32_t put_token = strat.legs[2].symbol_token;

            auto fut_it = orderbook.find(fut_token);
            auto call_it = orderbook.find(call_token);
            auto put_it = orderbook.find(put_token);

            // INLINE SNAPSHOT CREATION - NO FUNCTION CALLS
            StrategyMarketSnapshot snap;
            snap.kind = StrategyKind::CONREV_BID;
            snap.is_valid = true;

            // Direct assignment - fastest possible
            snap.data.three_leg_bidding.fut = fut_it->second;
            snap.data.three_leg_bidding.call = call_it->second;
            snap.data.three_leg_bidding.put = put_it->second;

            StrategyExecutor<StrategyKind::CONREV_BID>::run(strat, snap, order_manager, exe_time);

            LOG_TEST("Leg1 (Future) filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg1 (Future) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_over_all_fill += fill_qty;

            leg_id = 2;
            // Leg 2 (Call) filled completely
            order_data.leg2_filled = true;
            
            // added code of if modify reject cames after fill
            if(!order_data.leg2_ack)
            {
                order_data.leg2_covered_qty = order_data.last_covered_leg2;
            }
            order_data.leg2_ack = true;
            order_data.leg2_filled_qty += fill_qty;
            order_data.leg2_actual_fill_qty += fill_qty;
            order_data.leg2_pending_qty = 0;

            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += total_value;
            order_data.q2_call += fill_qty;
            order_data.leg2_order_id = 0;

            LOG_TEST("Leg2 (Call) filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_over_all_fill += fill_qty;

            leg_id = 3;
            // Leg 3 (Put) filled completely
            // added code of if modify reject cames after fill
            if(!order_data.leg3_ack)
            {
                order_data.leg3_covered_qty = order_data.last_covered_leg3;
            }
            order_data.leg3_ack = true;
            order_data.leg3_filled = true;
            order_data.leg3_filled_qty += fill_qty;
            order_data.leg3_pending_qty = 0;
            order_data.leg3_actual_fill_qty += fill_qty;

            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += total_value;
            order_data.q3_put += fill_qty;
            order_data.leg3_order_id = 0;

            LOG_TEST("Leg3 (Put) filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in fill: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in fill:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in fill:" + std::to_string(strategy_order_id));
        }

        // Check if complete arbitrage cycle is filled (all three legs with same quantity)
        if (order_data.leg2_filled && order_data.leg3_filled &&
            order_data.q1_fut > 0 && order_data.q1_fut == order_data.q2_call &&
            order_data.q2_call == order_data.q3_put)
        {
            LOG_FILE("TEMPLATE_HELPER",
                     "Complete arbitrage cycle filled. Achieved spread1: " + std::to_string(order_data.achieved_spread) +
                         " | strike_price: " + std::to_string(order_data.strike_price) +
                         " | price_fut: " + std::to_string(order_data.price_fut) +
                         " | price_call: " + std::to_string(order_data.price_call) +
                         " | price_put: " + std::to_string(order_data.price_put) +
                         " | is_conversion: " + std::to_string(order_data.is_conversion));

            // // Calculate achieved spread for this cycle
            // order_data.price_fut = order_data.v1_fut / order_data.q1_fut;
            // order_data.price_call = order_data.v2_call / order_data.q2_call;
            // order_data.price_put = order_data.v3_put / order_data.q3_put;

            // if (order_data.is_conversion)
            // {
            //     order_data.achieved_spread = order_data.strike_price - order_data.price_fut +
            //                                  order_data.price_call - order_data.price_put;
            // }
            // else
            // {
            //     order_data.achieved_spread = -order_data.strike_price + order_data.price_fut -
            //                                  order_data.price_call + order_data.price_put;
            // }

            strat.is_iter_over = true;
            LOG_TEST("Complete arbitrage cycle filled. Achieved spread: " << order_data.achieved_spread);
            LOG_FILE("TEMPLATE_HELPER", "Complete arbitrage cycle filled. Achieved spread: " + std::to_string(order_data.achieved_spread));
            LOG_FILE("TEMPLATE_HELPER",
                     "Complete arbitrage cycle filled. Achieved spread2: " + std::to_string(order_data.achieved_spread) +
                         " | strike_price: " + std::to_string(order_data.strike_price) +
                         " | price_fut: " + std::to_string(order_data.price_fut) +
                         " | price_call: " + std::to_string(order_data.price_call) +
                         " | price_put: " + std::to_string(order_data.price_put) +
                         " | is_conversion: " + std::to_string(order_data.is_conversion));
        }

        // Mark strategy as needing iteration
        LOG_FILE("TEMPLATE HELPER", "Printing achived spread and traded qty : " + std::to_string(order_data.achieved_spread) + "," + std::to_string(order_data.traded_qty));
        // strat.achieved_spread = order_data.achieved_spread;
        strat.traded_qty = order_data.traded_qty;
        // strat.is_active = true;

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;
        LOG_FILE("TEMPLATE",
                 "For Spread: strategy id: " + std::to_string(strategy_order_id) +
                     ", leg1 order id: " + std::to_string(order_data.leg1_order_id) +
                     ", leg2 order id: " + std::to_string(order_data.leg2_order_id) +
                     ", leg3 order id: " + std::to_string(order_data.leg3_order_id) +
                     ", leg4 order id: " + std::to_string(order_data.leg4_order_id) +
                     ", leg1 value: " + std::to_string(order_data.v1) +
                     ", leg2 value: " + std::to_string(order_data.v2) +
                     ", leg3 value: " + std::to_string(order_data.v3) +
                     ", leg4 value: " + std::to_string(order_data.v4) +
                     ", leg1 qty: " + std::to_string(order_data.q1) +
                     ", leg2 qty: " + std::to_string(order_data.q2) +
                     ", leg3 qty: " + std::to_string(order_data.q3) +
                     ", leg4 qty: " + std::to_string(order_data.q4) +
                     ", Fill Qty: " + std::to_string(fill_qty));

        if (strategy_order_id == order_data.leg1_order_id)
        {
            leg_id = 1;
            // Leg 1 (Future) filled completely
            order_data.leg1_filled = true;
            order_data.leg1_ack = true; // In case this comes before anything else
            order_data.leg1_filled_qty += fill_qty; // Complete fill, so set total filled qty
            order_data.leg1_pending_qty = 0;        // No more pending
            order_data.leg1_order_id = 0;
            order_data.traded_qty += fill_qty;

            // Update PnL tracking
            const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v1 += total_value;
            order_data.q1 += fill_qty;
            strat.is_active = true;
            strat.is_iter_over = false;
            order_data.mod_max = 0;
            order_data.new_max = 0;
            order_data.state = BoxBiddingStates::LEG1_FILLED;

            std::cout
                << "[FULL_FILL]"
                << " pf=" << strat.portfolio_id
                << " leg=" << leg_id
                << " oms=" << strategy_order_id
                << " current_price=" << fill_price
                << " total_qty=" << order_data.q1
                << std::endl;

            StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);

            LOG_TEST("Leg1 filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg1 filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            // order_data.leg2_over_all_fill += fill_qty;

            leg_id = 2;
            // Leg 2 (Call) filled completely
            order_data.leg2_filled = true;

            if(!order_data.leg2_ack)
            {
                order_data.leg2_covered_qty = order_data.last_covered_leg2;
            } // For cases where modify request is sent but there is no ack yet, so the covered qty doesent represent the reality
            order_data.leg2_ack = true; // In case this comes before anything else
            order_data.leg2_filled_qty += fill_qty;
            order_data.leg2_depth = 0;
            order_data.leg2_counter = 0;
            order_data.leg2_actual_fill_qty += fill_qty;
            order_data.leg2_pending_qty = 0;
            order_data.last_leg2_price = 0;
            //order_data.last_leg2_qty = 0;
            // order_data.last_covered_leg2 = 0;

            // Update PnL tracking
            const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v2 += total_value;
            order_data.q2 += fill_qty;
            order_data.leg2_order_id = 0;

            std::cout
                << "[FULL_FILL]"
                << " pf=" << strat.portfolio_id
                << " leg=" << leg_id
                << " oms=" << strategy_order_id
                << " current_price=" << fill_price
                << " total_qty=" << order_data.q2
                << std::endl;

            LOG_TEST("Leg2 filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg2  filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            leg_id = 3;
            // Leg 3 (Put) filled completely
            order_data.leg3_filled = true;
            if(!order_data.leg3_ack)
            {
                order_data.leg3_covered_qty = order_data.last_covered_leg3;
            } // For cases where modify request is sent but there is no ack yet, so the covered qty doesent represent the reality
            order_data.leg3_ack = true; // In case this comes before anything else
            order_data.leg3_filled_qty += fill_qty;
            order_data.leg3_pending_qty = 0;
            order_data.leg3_depth = 0;
            order_data.leg3_counter = 0;
            order_data.leg3_actual_fill_qty += fill_qty;
            order_data.last_leg3_price = 0;
            //order_data.last_leg3_qty = 0;
            // order_data.last_covered_leg3 = 0;

            // Update PnL tracking
            const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v3 += total_value;
            order_data.q3 += fill_qty;
            order_data.leg3_order_id = 0;

            std::cout
                << "[FULL_FILL]"
                << " pf=" << strat.portfolio_id
                << " leg=" << leg_id
                << " oms=" << strategy_order_id
                << " current_price=" << fill_price
                << " total_qty=" << order_data.q3
                << std::endl;

            LOG_TEST("Leg3 filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            leg_id = 4;
            // Leg 4 (Put) filled completely
            order_data.leg4_filled = true;
            if(!order_data.leg4_ack)
            {
                order_data.leg4_covered_qty = order_data.last_covered_leg4;
            } // For cases where modify request is sent but there is no ack yet, so the covered qty doesent represent the reality
            order_data.leg4_ack = true; // In case this comes before anything else
            order_data.leg4_filled_qty += fill_qty;
            order_data.leg4_pending_qty = 0;
            order_data.leg4_depth = 0;
            order_data.leg4_counter = 0;
            order_data.leg4_actual_fill_qty += fill_qty;
            order_data.last_leg4_price = 0;
            //order_data.last_leg4_qty = 0;
            // order_data.last_covered_leg4 = 0;

            // Update PnL tracking
            const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v4 += total_value;
            order_data.q4 += fill_qty;
            order_data.leg4_order_id = 0;

            std::cout
                << "[FULL_FILL]"
                << " pf=" << strat.portfolio_id
                << " leg=" << leg_id
                << " oms=" << strategy_order_id
                << " current_price=" << fill_price
                << " total_qty=" << order_data.q4
                << std::endl;

            LOG_TEST("Leg4 filled completely: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg4 filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in fill: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in fill:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in fill:" + std::to_string(strategy_order_id));
        }

        LOG_FILE("TemplateHelper", "Print condition for checking if the entire cycle is completed: order_data.leg1_filled: " + std::to_string(order_data.leg1_filled) +
                                       "order_data.leg2_filled: " + std::to_string(order_data.leg2_filled) +
                                       "order_data.leg3_filled: " + std::to_string(order_data.leg3_filled) +
                                       "order_data.leg4_filled: " + std::to_string(order_data.leg4_filled) +
                                       "order_data.q1_call: " + std::to_string(order_data.q1) +
                                       "order_data.q2_put: " + std::to_string(order_data.q2) +
                                       "order_data.q3_call: " + std::to_string(order_data.q3) +
                                       "order_data.q4_put: " + std::to_string(order_data.q4));

        // Check if complete bidding cycle is filled (all four legs with same quantity)
        if (order_data.leg1_filled && order_data.leg2_filled && order_data.leg3_filled && order_data.leg4_filled &&
            order_data.q1 > 0 && order_data.q1 == order_data.q2 &&
            order_data.q2 == order_data.q3 && order_data.q3 == order_data.q4)
        {
            LOG_FILE("TEMPLATE_HELPER",
                     "Complete Four Leg Bidding cycle filled. Achieved spread1: " + std::to_string(order_data.achieved_spread) +
                         " | strike_price: " + std::to_string(order_data.strike_diff) +
                         " | price_leg1_for_entire_cycle: " + std::to_string(order_data.price_leg1_for_entire_cycle) +
                         " | price_leg2_for_entire_cycle: " + std::to_string(order_data.price_leg2_for_entire_cycle) +
                         " | price_leg3_for_entire_cycle: " + std::to_string(order_data.price_leg3_for_entire_cycle) +
                         " | price_leg4_for_entire_cycle: " + std::to_string(order_data.price_leg4_for_entire_cycle) +
                         " | is_conversion: " + std::to_string(order_data.is_flip));

            // Calculate achieved spread for this cycle
            order_data.price_leg1_for_entire_cycle = order_data.v1 / order_data.q1;
            order_data.price_leg2_for_entire_cycle = order_data.v2 / order_data.q2;
            order_data.price_leg3_for_entire_cycle = order_data.v3 / order_data.q3;
            order_data.price_leg4_for_entire_cycle = order_data.v3 / order_data.q3;

            if (order_data.is_flip)
            {
                order_data.achieved_spread = order_data.strike_diff - order_data.price_leg1_for_entire_cycle +
                                             order_data.price_leg3_for_entire_cycle + order_data.price_leg2_for_entire_cycle - order_data.price_leg4_for_entire_cycle;
            }
            else
            {
                order_data.achieved_spread = order_data.strike_diff + order_data.price_leg1_for_entire_cycle -
                                             order_data.price_leg3_for_entire_cycle - order_data.price_leg2_for_entire_cycle + order_data.price_leg4_for_entire_cycle;
            }

            std::cout
                << "[CYCLE_COMPLETE]"
                << " pf=" << strat.portfolio_id
                << " qty=" << order_data.q1
                << " spread=" << order_data.achieved_spread
                << std::endl;

            LOG_TEST("Complete BoxBidding cycle filled. Achieved spread: " << order_data.achieved_spread);
            LOG_FILE("TEMPLATE_HELPER", "Complete BoxBidding cycle filled. Achieved spread: " + std::to_string(order_data.achieved_spread));
            LOG_FILE("TEMPLATE_HELPER",
                     "Complete BoxBidding cycle filled. Achieved spread1: " + std::to_string(order_data.achieved_spread) +
                         " | strike_price: " + std::to_string(order_data.strike_diff) +
                         " | price_leg1_for_entire_cycle: " + std::to_string(order_data.price_leg1_for_entire_cycle) +
                         " | price_leg2_for_entire_cycle: " + std::to_string(order_data.price_leg2_for_entire_cycle) +
                         " | price_leg3_for_entire_cycle: " + std::to_string(order_data.price_leg3_for_entire_cycle) +
                         " | price_leg4_for_entire_cycle: " + std::to_string(order_data.price_leg4_for_entire_cycle) +
                         " | is_conversion: " + std::to_string(order_data.is_flip));

            uint32_t total_traded_qty_in_this_cycle = StrategyExecutor<StrategyKind::BOX_1_1_1_1>::getTotalTradedQty(order_data);
            LOG_FILE("TemplateHelper", "Total traded qty in this cycle: " + std::to_string(total_traded_qty_in_this_cycle));
            order_data.traded_qty = total_traded_qty_in_this_cycle; /// technically filled qty variables of all legs should be same for each cycle

            strat.achieved_spread += order_data.achieved_spread;
            strat.traded_qty += order_data.traded_qty;
            LOG_FILE("TEMPLATE HELPER", "Printing achived spread and traded qty : " + std::to_string(order_data.achieved_spread) + "," + std::to_string(order_data.traded_qty));

            if (strat.traded_qty < strat.params.box_bidding.max_lots)
            {
                strat.is_data_updated = true;

                // Log the update for debugging
                LOG_FILE("TemplateHelper", "One iteration over, total traded qty: " + std::to_string(order_data.traded_qty));
                strat.is_iter_over = true;

                order_data.state = BoxBiddingStates::COMPLETED;
                StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);
            }
            else
            {
                LOG_FILE("TemplateHelper", "STOP - COMPLETED");
                LOG_FILE("TemplateHelper", "Going to handle Completed State: " + std::to_string(strat.traded_qty));
                strat.terminate = true;
                strat.is_iter_over = true;

                order_data.state = BoxBiddingStates::COMPLETED;
                StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);
            }
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleOrderFill(strat, strategy_order_id, fill_qty, fill_price, order_manager, exe_time);
        break;
    }
    default:
    {

        LOG_LIVE("TEMPLATE_HELPER", "Default case of on Handle Fill");
        break;
    }
    }
}

ALWAYS_INLINE void handlePartialFill(Portfolio &strat, uint32_t strategy_order_id, uint32_t fill_qty, uint32_t fill_price, uint32_t required_qty, int32_t &leg_id, OrderManager &order_manager, unsigned long long exe_time, ska::flat_hash_map<uint32_t, StoredMarketDataLatency> &orderbook) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC:
    {
        auto &order_data = strat.order_data.conrev;

        uint8_t leg = order_data.order_id_to_leg.lookup(strategy_order_id);
        switch (leg)
        {
        case 0:
        {
            leg_id = 1;
            order_data.traded_qty += fill_qty;
            order_data.remain_qty -= fill_qty; // not using right now anywhere
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v1_fut += total_value;
            // order_data.q1_fut += fill_qty;

            LOG_TEST("Leg1 (Future) Partial filled : qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg1 (Future) Partial filled: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* fut */ break;
        }
        case 1:
        {
            leg_id = 2;
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += total_value;
            // order_data.q2_call += fill_qty;

            LOG_TEST("Leg2 (Call) Partial filled: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  Partial filled: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* call */ break;
        }

        case 2:
        {
            leg_id = 3;
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += total_value;
            // order_data.q3_put += fill_qty;

            LOG_TEST("Leg3 (Put) Partial filled: qty=" << fill_qty << " price=" << fill_price);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) Partial filled: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* put */ break;
        }
        default:
            LOG_FILE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            LOG_LIVE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            break;
        }

        strat.traded_qty = order_data.traded_qty;

        break;
    }

    case StrategyKind::CONREV_BID:
    {

        auto &order_data = strat.order_data.three_leg_bidding;

        LOG_FILE("TEMPLATE",
                 "For Spread: strategy id: " + std::to_string(strategy_order_id) +
                     ", leg1 order id: " + std::to_string(order_data.leg1_order_id) +
                     ", leg2 order id: " + std::to_string(order_data.leg2_order_id) +
                     ", leg3 order id: " + std::to_string(order_data.leg3_order_id) +
                     ", FUT value: " + std::to_string(order_data.v1_fut) +
                     ", CALL value: " + std::to_string(order_data.v2_call) +
                     ", PUT value: " + std::to_string(order_data.v3_put) +
                     ", FUT qty: " + std::to_string(order_data.q1_fut) +
                     ", CALL qty: " + std::to_string(order_data.q2_call) +
                     ", PUT qty: " + std::to_string(order_data.q3_put));

        if (strategy_order_id == order_data.leg1_order_id)
        {
            order_data.leg1_over_all_fill += fill_qty;

            leg_id = 1;
            // Leg 1 (Future) partially filled
            order_data.leg1_partial_filled = true;
            order_data.leg1_filled_qty += fill_qty;
            order_data.leg1_pending_qty = required_qty - order_data.leg1_filled_qty;

            // Update PnL tracking for partial fill
            // const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v1_fut += partial_value;
            order_data.q1_fut += fill_qty;
            strat.is_active = true;
            strat.is_iter_over = false;
            order_data.mod_max = 0;
            order_data.new_max = 0;

            order_data.state = ThreeLegBiddingState::LEG1_PARTIAL_FILLED;

            const uint32_t fut_token = strat.legs[0].symbol_token;
            const uint32_t call_token = strat.legs[1].symbol_token;
            const uint32_t put_token = strat.legs[2].symbol_token;

            auto fut_it = orderbook.find(fut_token);
            auto call_it = orderbook.find(call_token);
            auto put_it = orderbook.find(put_token);

            // INLINE SNAPSHOT CREATION - NO FUNCTION CALLS
            StrategyMarketSnapshot snap;
            snap.kind = StrategyKind::CONREV_BID;
            snap.is_valid = true;

            // Direct assignment - fastest possible
            snap.data.three_leg_bidding.fut = fut_it->second;
            snap.data.three_leg_bidding.call = call_it->second;
            snap.data.three_leg_bidding.put = put_it->second;

            StrategyExecutor<StrategyKind::CONREV_BID>::run(strat, snap, order_manager, exe_time);

            LOG_TEST("Leg1 (Future) partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg1_filled_qty
                                                        << " pending=" << order_data.leg1_pending_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg1 (Future) partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg1_filled_qty) + " pending=" + std::to_string(order_data.leg1_pending_qty));
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            leg_id = 2;
            order_data.leg2_over_all_fill += fill_qty;

            // Leg 2 (Call) partially filled
            order_data.leg2_filled_qty += fill_qty;
            order_data.leg2_actual_fill_qty += fill_qty;
            order_data.leg2_pending_qty = required_qty - order_data.leg2_filled_qty;

            // Check if completely filled now
            // if (order_data.leg2_filled_qty >= required_qty)
            // {
            //     order_data.leg2_filled = true;
            // }

            // Update PnL tracking
            // const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += partial_value;
            order_data.q2_call += fill_qty;

            LOG_TEST("Leg2 (Call) partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg2_filled_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call) partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg2_filled_qty));
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            leg_id = 3;
            order_data.leg3_over_all_fill += fill_qty;

            // Leg 3 (Put) partially filled
            order_data.leg3_filled_qty += fill_qty;
            order_data.leg3_pending_qty = required_qty - order_data.leg3_filled_qty;
            order_data.leg3_actual_fill_qty += fill_qty;
            // Check if completely filled now
            // if (order_data.leg3_filled_qty >= required_qty)
            // {
            //     order_data.leg3_filled = true;
            // }

            // Update PnL tracking
            // const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += partial_value;
            order_data.q3_put += fill_qty;

            LOG_TEST("Leg3 (Put) partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg3_filled_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg3_filled_qty));
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in partial fill: " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER","Unknown strategy order id received in partial fill: " +  std::to_string( strategy_order_id));
        }

        // Check if complete arbitrage cycle is filled (all three legs with same quantity)
        if (order_data.leg2_filled && order_data.leg3_filled &&
            order_data.q1_fut > 0 && order_data.q1_fut == order_data.q2_call &&
            order_data.q2_call == order_data.q3_put)
        {
            LOG_FILE("TEMPLATE_HELPER",
                     "Complete arbitrage cycle filled. Achieved spread3: " + std::to_string(order_data.achieved_spread) +
                         " | strike_price: " + std::to_string(order_data.strike_price) +
                         " | price_fut: " + std::to_string(order_data.price_fut) +
                         " | price_call: " + std::to_string(order_data.price_call) +
                         " | price_put: " + std::to_string(order_data.price_put) +
                         " | is_conversion: " + std::to_string(order_data.is_conversion));
            // Calculate achieved spread for this cycle
            // order_data.price_fut = order_data.v1_fut / order_data.q1_fut;
            // order_data.price_call = order_data.v2_call / order_data.q2_call;
            // order_data.price_put = order_data.v3_put / order_data.q3_put;

            // if (order_data.is_conversion)
            // {
            //     order_data.achieved_spread = order_data.strike_price - order_data.price_fut +
            //                                  order_data.price_call - order_data.price_put;
            // }
            // else
            // {
            //     order_data.achieved_spread = -order_data.strike_price + order_data.price_fut -
            //                                  order_data.price_call + order_data.price_put;
            // }

            LOG_FILE("TEMPLATE_HELPER",
                     "Complete arbitrage cycle filled. Achieved spread4: " + std::to_string(order_data.achieved_spread) +
                         " | strike_price: " + std::to_string(order_data.strike_price) +
                         " | price_fut: " + std::to_string(order_data.price_fut) +
                         " | price_call: " + std::to_string(order_data.price_call) +
                         " | price_put: " + std::to_string(order_data.price_put) +
                         " | is_conversion: " + std::to_string(order_data.is_conversion));

            // strat.is_iter_over = true;
            LOG_TEST("Complete arbitrage cycle filled. Achieved spread: " << order_data.achieved_spread);
        }

        // Mark strategy as needing iteration for handling partial fills

        // strat.achieved_spread = order_data.achieved_spread;
        strat.traded_qty = order_data.traded_qty;
        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        LOG_FILE("TemplateHelper",
                 "For Spread: strategy id: " + std::to_string(strategy_order_id) +
                     ", leg1 order id: " + std::to_string(order_data.leg1_order_id) +
                     ", leg2 order id: " + std::to_string(order_data.leg2_order_id) +
                     ", leg3 order id: " + std::to_string(order_data.leg3_order_id) +
                     ", leg4 order id: " + std::to_string(order_data.leg4_order_id) +
                     ", leg1 value: " + std::to_string(order_data.v1) +
                     ", leg2 value: " + std::to_string(order_data.v2) +
                     ", leg3 value: " + std::to_string(order_data.v3) +
                     ", leg4 value: " + std::to_string(order_data.v4) +
                     ", leg1 qty: " + std::to_string(order_data.q1) +
                     ", leg2 qty: " + std::to_string(order_data.q2) +
                     ", leg3 qty: " + std::to_string(order_data.q3) +
                     ", leg4 qty: " + std::to_string(order_data.q4) +
                     ", Partial Fill Qty: " + std::to_string(fill_qty));

        if (strategy_order_id == order_data.leg1_order_id)
        {
            leg_id = 1;
            // Leg 1 (Future) partially filled
            order_data.leg1_partial_filled = true;
            order_data.leg1_filled_qty += fill_qty;
            order_data.leg1_pending_qty = required_qty - order_data.leg1_filled_qty;
            order_data.traded_qty += fill_qty;

            // Update PnL tracking for partial fill
            const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v1 += partial_value;
            order_data.q1 += fill_qty;
            strat.is_active = true;
            strat.is_iter_over = false;
            order_data.mod_max = 0;
            order_data.new_max = 0;

            std::cout
                        << "[PARTIAL_FILL]"
                        << " pf=" << strat.portfolio_id
                        << " leg=" << leg_id
                        << " oms=" << strategy_order_id
                        << " fill_px=" << fill_price
                        << " fill_qty=" << fill_qty
                        << " cum_fill=" << order_data.q1
                        << std::endl;

            order_data.state = BoxBiddingStates::LEG1_PARTIAL_FILLED;
            StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);

            LOG_TEST("Leg1 partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg1_filled_qty
                                               << " pending=" << order_data.leg1_pending_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg1 partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg1_filled_qty) + " pending=" + std::to_string(order_data.leg1_pending_qty));
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            leg_id = 2;
            order_data.leg2_filled_qty += fill_qty;
            order_data.leg2_actual_fill_qty += fill_qty;
            order_data.leg2_pending_qty = required_qty - order_data.leg2_filled_qty;
            order_data.last_leg2_price = fill_price;
            order_data.last_leg2_qty = required_qty - fill_qty;

            // Update PnL tracking
            const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v2 += partial_value;
            order_data.q2 += fill_qty;

            std::cout
                        << "[PARTIAL_FILL]"
                        << " pf=" << strat.portfolio_id
                        << " leg=" << leg_id
                        << " oms=" << strategy_order_id
                        << " fill_px=" << fill_price
                        << " fill_qty=" << fill_qty
                        << " cum_fill=" << order_data.q2
                        << std::endl;

            LOG_TEST("Leg2 partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg2_filled_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg2 partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg2_filled_qty));
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            leg_id = 3;

            // Leg 3 (Put) partially filled
            order_data.leg3_filled_qty += fill_qty;
            order_data.leg3_pending_qty = required_qty - order_data.leg3_filled_qty;
            order_data.leg3_actual_fill_qty += fill_qty;
            order_data.last_leg3_price = fill_price;
            order_data.last_leg3_qty = required_qty - fill_qty;

            // Update PnL tracking
            const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v3 += partial_value;
            order_data.q3 += fill_qty;

            std::cout
                        << "[PARTIAL_FILL]"
                        << " pf=" << strat.portfolio_id
                        << " leg=" << leg_id
                        << " oms=" << strategy_order_id
                        << " fill_px=" << fill_price
                        << " fill_qty=" << fill_qty
                        << " cum_fill=" << order_data.q3
                        << std::endl;

            LOG_TEST("Leg3 partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg3_filled_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg3_filled_qty));
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            leg_id = 4;

            // Leg 3 (Put) partially filled
            order_data.leg4_filled_qty += fill_qty;
            order_data.leg4_pending_qty = required_qty - order_data.leg4_filled_qty;
            order_data.leg4_actual_fill_qty += fill_qty;
            order_data.last_leg4_price = fill_price;
            order_data.last_leg4_qty = required_qty - fill_qty;

            // Update PnL tracking
            const uint64_t partial_value = static_cast<uint64_t>(fill_qty) * fill_price;
            order_data.v4 += partial_value;
            order_data.q4 += fill_qty;

            std::cout
                        << "[PARTIAL_FILL]"
                        << " pf=" << strat.portfolio_id
                        << " leg=" << leg_id
                        << " oms=" << strategy_order_id
                        << " fill_px=" << fill_price
                        << " fill_qty=" << fill_qty
                        << " cum_fill=" << order_data.q4
                        << std::endl;

            LOG_TEST("Leg3 partial fill: qty=" << fill_qty << " total_filled=" << order_data.leg4_filled_qty);
            LOG_FILE("TEMPLATE_HELPER", "Leg3 partial fill: qty=" + std::to_string(fill_qty) + " total_filled=" + std::to_string(order_data.leg4_filled_qty));
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in partial fill: " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER","Unknown strategy order id received in partial fill: " + std::to_string( strategy_order_id));
        }
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handlePartialFill(strat, strategy_order_id, fill_qty, required_qty, fill_price, order_manager, exe_time);
        break;
    }
    default:
    {

        LOG_LIVE("TEMPLATE_HELPER", "Default case of on Handle Fill");

        break;
    }
    }
}

ALWAYS_INLINE void handleReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty, uint32_t fill_qty_sum) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC:
    {
        auto &order_data = strat.order_data.conrev;
        uint8_t leg = order_data.order_id_to_leg.lookup(strategy_order_id);
        switch (leg)
        {
        case 0:
        {
            if (required_qty > fill_qty_sum)
            {
                order_data.ordered_qty = order_data.ordered_qty - (required_qty - fill_qty_sum);
            }
            /* fut */ break;
        }
        case 1:
        {
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += total_value;
            // order_data.q2_call += fill_qty;

            // LOG_TEST("Leg2 (Call) filled completely: qty=" << fill_qty << " price=" << fill_price);
            // LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* call */ break;
        }

        case 2:
        {
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += total_value;
            // order_data.q3_put += fill_qty;

            // LOG_TEST("Leg3 (Put) filled completely: qty=" << fill_qty << " price=" << fill_price);
            // LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* put */ break;
        }
        default:
            LOG_FILE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            break;
        }

        // if (strategy_order_id == order_data.leg1_order_id)
        // {
        // if (required_qty > fill_qty_sum)
        // {
        //     order_data.ordered_qty = order_data.ordered_qty - (required_qty - fill_qty_sum);
        // }

        // // if (order_data.ordered_qty >= required_qty)
        // // {
        // //     order_data.ordered_qty = order_data.ordered_qty - required_qty;
        // // }
        // // else
        // // {
        // //     LOG_FILE("TEMPLATE_HELPER", "ERROR ORDERED QTY LESS THAN REQ QTY");
        // // }
        // }
        strat.is_iter_over = true;
        if (!strat.stop_requested)
        {
            strat.is_active = true;
        }
        break;
    }

    case StrategyKind::CONREV_BID:
    {
        auto &order_data = strat.order_data.three_leg_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            // strat.is_iter_over = true;
            // Leg 1 (Future) order rejected
            strat.is_iter_over = true;
            order_data.leg1_order_id = 0; // Clear order ID
            order_data.leg1_pending_qty = 0;
            order_data.leg1_ack = true;

            // If leg 1 is rejected, we need to go back to IDLE state to retry or exit
            if (order_data.state == ThreeLegBiddingState::LEG1_PENDING)
            {
                order_data.state = ThreeLegBiddingState::IDLE;
            }

            if (order_data.new_max > order_data.limit_new_max || order_data.mod_max > order_data.limit_mod_max)
            {
                LOG_FILE("TemplateHelper", "came here new max is reached or mod max reached => new max ,mod max is : " + std::to_string(order_data.new_max) + "  ," + std::to_string(order_data.mod_max));
                LOG_LIVE("TemplateHelper", "came here new max is reached or mod max reached => new max ,mod max is : " + std::to_string(order_data.new_max) + "  ," + std::to_string(order_data.mod_max));

                strat.is_active = false;
                strat.stop_requested = true;
                strat.stop_reason = order_data.new_max > order_data.limit_new_max ? UpdateReason::NewMax : UpdateReason::ModMax;
            }

            // if (order_data.leg1_over_all_fill > 0 && std::min(order_data.leg2_over_all_fill, order_data.leg3_over_all_fill) < order_data.leg1_over_all_fill)
            // {
            //     order_data.state = ThreeLegBiddingState::LEG1_FILLED;
            //     strat.is_active = true;
            //     strat.is_iter_over = false;
            //     StrategyExecutor<StrategyKind::CONREV_BID>::run(strat, order_data.last_market_snapshot, order_manager, exe_time);
            // }

            LOG_FILE("TEMPLATE_HELPER", "Leg1 (Future) order rejected: qty=" + std::to_string(required_qty));
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            // Leg 2 (Call) order rejected
            order_data.leg2_order_id = 0; // Clear order ID
            order_data.leg2_filled = false;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_covered_qty -= required_qty;

            // Reset cover leg counters
            order_data.leg2_counter = 0;
            order_data.leg2_depth = 0;
            order_data.last_leg2_price = 0;
            order_data.last_leg2_qty = 0;

            order_data.leg2_pending_qty = 0;
            order_data.leg2_ack = true;

            LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call) order rejected: qty=" + std::to_string(required_qty));
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            // Leg 3 (Put) order rejected
            order_data.leg3_order_id = 0; // Clear order ID
            order_data.leg3_filled = false;
            order_data.leg3_covered_qty -= required_qty;
            order_data.leg3_filled_qty = 0;

            // Reset cover leg counters
            order_data.leg3_counter = 0;
            order_data.leg3_depth = 0;
            order_data.last_leg3_price = 0;
            order_data.last_leg3_qty = 0;

            order_data.leg3_ack = true;
            order_data.leg3_pending_qty = 0;

            LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put)  order rejected: qty=" + std::to_string(required_qty));
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in reject: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in reject:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in reject:" + std::to_string(strategy_order_id));
        }

        break;
    }

        // Updated handleReject for BoxBidding
    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            // Leg 1 order rejected
            order_data.leg1_order_id = 0; // Clear order ID
            order_data.leg1_pending_qty = 0;
            order_data.leg1_filled_qty = 0;
            order_data.leg1_ack = true;

            // If leg 1 is rejected, we need to go back to IDLE state to retry or exit
            if (order_data.state == BoxBiddingStates::LEG1_PENDING)
            {
                LOG_FILE("TemplateStrat", "order state when Leg1 reject came: " + std::to_string((int)order_data.state));
                order_data.state = BoxBiddingStates::IDLE;
                // StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(p,s,o,)
                //  ideally to run krvu joiye
            }

            LOG_FILE("TEMPLATE_HELPER", "Leg1 order rejected: qty=" + std::to_string(required_qty));
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            // Leg 2 order rejected
            order_data.leg2_order_id = 0; // Clear order ID
            order_data.leg2_filled = false;
            order_data.leg2_ack = true;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_covered_qty = order_data.last_covered_leg2;

            // Reset cover leg counters
            order_data.leg2_counter = 0;
            order_data.leg2_depth = 0;
            order_data.last_leg2_price = 0;
            order_data.last_leg2_qty = 0;

            order_data.leg2_pending_qty = 0;

            LOG_FILE("TEMPLATE_HELPER", "Leg2 order rejected: qty=" + std::to_string(required_qty));
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            // Leg 3 (Put) order rejected
            order_data.leg3_order_id = 0; // Clear order ID
            order_data.leg3_filled = false;
            order_data.leg3_ack = true;
            order_data.leg3_covered_qty = order_data.last_covered_leg3;
            order_data.leg3_filled_qty = 0;

            // Reset cover leg counters
            order_data.leg3_counter = 0;
            order_data.leg3_depth = 0;
            order_data.last_leg3_price = 0;
            order_data.last_leg3_qty = 0;

            order_data.leg3_pending_qty = 0;

            LOG_FILE("TEMPLATE_HELPER", "Leg3 order rejected: qty=" + std::to_string(required_qty));
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            // Leg 3 (Put) order rejected
            order_data.leg4_order_id = 0; // Clear order ID
            order_data.leg4_filled = false;
            order_data.leg4_ack = true;
            order_data.leg4_actual_fill_qty = 0;
            order_data.leg4_covered_qty = order_data.last_covered_leg4;
            order_data.leg4_filled_qty = 0;

            // Reset cover leg counters
            order_data.leg4_counter = 0;
            order_data.leg4_depth = 0;
            order_data.last_leg4_price = 0;
            order_data.last_leg4_qty = 0;

            order_data.leg4_pending_qty = 0;

            LOG_FILE("TEMPLATE_HELPER", "Leg4 order rejected: qty=" + std::to_string(required_qty));
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in reject: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in reject:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in reject:" + std::to_string(strategy_order_id));
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleOrderReject(strat, strategy_order_id);
        LOG_FILE("TEMPLATE_HELPER", "Exititing TEMPLATE_HELPER ");
        break;
    }
    default:
        break;
    }
}

ALWAYS_INLINE void handleCancel(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty, uint32_t fill_qty_sum, OrderManager &order_manager, unsigned long long exe_time) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC:
    {
        auto &order_data = strat.order_data.conrev;

        uint8_t leg = order_data.order_id_to_leg.lookup(strategy_order_id);
        LOG_FILE("TEMPLATE_HANDLE", "leg id:" + std::to_string(static_cast<int>(leg)) + ", oms orde rid: " + std::to_string(strategy_order_id) + " required_qty: " + std::to_string(required_qty) + " fill_qty_sum: " + std::to_string(fill_qty_sum));

        switch (leg)
        {
        case 0:
        {
            if (required_qty > fill_qty_sum)
            {
                order_data.ordered_qty = order_data.ordered_qty - (required_qty - fill_qty_sum);
                LOG_FILE("TEMPLATE_HANDLE", "Ordered qty:" + std::to_string(order_data.ordered_qty));
            }
            /* fut */ break;
        }
        case 1:
        {
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += total_value;
            // order_data.q2_call += fill_qty;

            // LOG_TEST("Leg2 (Call) filled completely: qty=" << fill_qty << " price=" << fill_price);
            // LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* call */ break;
        }

        case 2:
        {
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += total_value;
            // order_data.q3_put += fill_qty;

            // LOG_TEST("Leg3 (Put) filled completely: qty=" << fill_qty << " price=" << fill_price);
            // LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* put */ break;
        }
        default:
            LOG_FILE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            break;
        }

        strat.is_iter_over = true; // TODO not interation over over when all acks will get some response but for CONREV_IOC its fine
        if (!strat.stop_requested)
        {
            strat.is_active = true;
        }

        break;
    }
    case StrategyKind::CONREV_BID:
    {
        auto &order_data = strat.order_data.three_leg_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            // Leg 1 (Future) order cancelled
            order_data.leg1_order_id = 0; // Clear order ID
            order_data.leg1_pending_qty = 0;
            order_data.leg1_cancel_request = false;

            // // If we're in LEG1_PENDING state and leg1 is cancelled, go back to IDLE
            // if (order_data.state == CONREV_BIDState::LEG1_PENDING)
            // {
            //     order_data.state = CONREV_BIDState::IDLE;
            // }

            order_data.state = ThreeLegBiddingState::IDLE;
            // order_data.new_max++;
            if (order_data.new_max > order_data.limit_new_max || order_data.mod_max > order_data.limit_mod_max)
            {
                LOG_FILE("TemplateHelper", "came here new max is reached or mod max reached => new max ,mod max is : " + std::to_string(order_data.new_max) + "  ," + std::to_string(order_data.mod_max));
                LOG_LIVE("TemplateHelper", "came here new max is reached or mod max reached => new max ,mod max is : " + std::to_string(order_data.new_max) + "  ," + std::to_string(order_data.mod_max));

                strat.is_active = false;
                strat.stop_requested = true;
                strat.is_iter_over = true;
                strat.stop_reason = order_data.new_max > order_data.limit_new_max ? UpdateReason::NewMax : UpdateReason::ModMax;
            }

            if (order_data.leg1_over_all_fill > 0 && std::min(order_data.leg2_over_all_fill, order_data.leg3_over_all_fill) < order_data.leg1_over_all_fill)
            {
                order_data.state = ThreeLegBiddingState::LEG1_FILLED;
                strat.is_active = true;
                strat.is_iter_over = false;
                StrategyExecutor<StrategyKind::CONREV_BID>::run(strat, order_data.last_market_snapshot, order_manager, exe_time);
            }

            LOG_TEST("Leg1 (Future) order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            // DUE TO SELF TRADE IT MAY GOES CANCEL HENCE HANDLED CASE
            // Leg 2 (Call) order cancelled
            order_data.leg2_order_id = 0; // Clear order ID
            order_data.leg2_filled = false;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_covered_qty -= required_qty;

            // Reset cover leg counters
            order_data.leg2_counter = 0;
            order_data.leg2_depth = 0;
            order_data.last_leg2_price = 0;
            order_data.last_leg2_qty = 0;

            order_data.leg2_pending_qty = 0;
            order_data.leg2_ack = true;
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            // DUE TO SELF TRADE IT MAY GOES CANCEL HENCE HANDLED CASE
            // Leg 3 (Put) order cancelled
            order_data.leg3_order_id = 0; // Clear order ID
            order_data.leg3_filled = false;
            order_data.leg3_covered_qty -= required_qty;
            order_data.leg3_filled_qty = 0;

            // Reset cover leg counters
            order_data.leg3_counter = 0;
            order_data.leg3_depth = 0;
            order_data.last_leg3_price = 0;
            order_data.last_leg3_qty = 0;

            order_data.leg3_ack = true;
            order_data.leg3_pending_qty = 0;
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in cancel: " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER","Unknown strategy order id received in cancel: " + std::to_string( strategy_order_id));

        }

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            // Leg 1 order cancelled
            order_data.leg1_order_id = 0; // Clear order ID
            order_data.leg1_pending_qty = 0;
            order_data.leg1_ack = true;

            LOG_FILE("TemplateHelper", "Cancel came for leg1");

            order_data.state = BoxBiddingStates::IDLE;

            if (strat.stop_requested && order_data.q1 > 0 && order_data.q1==order_data.q2 && order_data.q2==order_data.q3 && order_data.q3==order_data.q4)
            {

                LOG_FILE("TemplateHelper", "Cancel ack for leg1, stop requested: " + std::to_string(strat.stop_requested));
                // strat.terminate = true;
                strat.is_iter_over = true;

                order_data.state = BoxBiddingStates::COMPLETED;
                StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);
            }
            if (order_data.new_max > CANCEL_MAX_4Leg_BIDDING || order_data.mod_max > MOD_MAX_4Leg_BIDDING)
            {
                LOG_FILE("TemplateHelper", "came here new max is reached or mod max reached => new max ,mod max is : " + std::to_string(order_data.new_max) + "  ," + std::to_string(order_data.mod_max));
                LOG_LIVE("TemplateHelper", "Cancel Order Max Reached Stopping strategy");
                // strat.stop_requested = true;
                // strat.terminate = true;
                strat.stop_requested = true;
                strat.stop_reason = order_data.new_max > CANCEL_MAX_4Leg_BIDDING ? UpdateReason::NewMax : UpdateReason::ModMax;
                strat.is_iter_over = true;
                order_data.state = BoxBiddingStates::COMPLETED;
                StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);
            }

            if (order_data.leg1_filled_qty > 0 && std::min({order_data.leg2_actual_fill_qty, order_data.leg3_actual_fill_qty, order_data.leg4_actual_fill_qty}) < order_data.leg1_filled_qty)
            {
                order_data.state = BoxBiddingStates::LEG1_FILLED;
                order_data.leg1_filled = true;
                strat.is_active = true;
                strat.is_iter_over = false;
                StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);

                std::cout << "In a critical state: bidding leg cancel after partial fill" << std::endl;
            }

            LOG_TEST("Leg1 order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            // Leg 2  order cancelled
            order_data.leg2_order_id = 0; // Clear order ID

            // Adjust current hedge quantity if this leg was part of hedge
            if (order_data.current_hedge_qty >= required_qty)
            {
                order_data.current_hedge_qty -= required_qty;
            }

            // Reset cover leg state
            order_data.leg2_filled = false;
            order_data.leg2_counter = 0;
            order_data.leg2_depth = 0;
            order_data.leg2_covered_qty -= order_data.leg2_pending_qty;
            order_data.leg2_pending_qty = 0;
            order_data.leg2_ack = true;
            order_data.last_leg2_price = 0;
            order_data.last_leg2_qty = order_data.leg2_filled_qty;
            order_data.leg2_filled_qty = 0;
            order_data.leg2_price = 0;
            order_data.last_covered_leg2 = order_data.leg2_covered_qty;

            LOG_TEST("Leg2 order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            // Leg 3 (OTM Call) order cancelled
            order_data.leg3_order_id = 0; // Clear order ID

            // Adjust current hedge quantity if this leg was part of hedge
            if (order_data.current_hedge_qty >= required_qty)
            {
                order_data.current_hedge_qty -= required_qty;
            }

            // Reset cover leg state
            order_data.leg3_filled = false;
            order_data.leg3_counter = 0;
            order_data.leg3_depth = 0;
            order_data.leg3_ack = true;
            order_data.last_leg3_price = 0;
            order_data.leg3_covered_qty -= order_data.leg3_pending_qty;
            order_data.leg3_pending_qty = 0;
            order_data.last_leg3_qty = order_data.leg3_filled_qty;
            order_data.leg3_filled_qty = 0;
            order_data.leg3_price = 0;
            order_data.last_covered_leg3 = order_data.leg3_covered_qty;

            LOG_TEST("Leg3 order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            // Leg 3 (OTM Call) order cancelled
            order_data.leg4_order_id = 0; // Clear order ID

            // Adjust current hedge quantity if this leg was part of hedge
            if (order_data.current_hedge_qty >= required_qty)
            {
                order_data.current_hedge_qty -= required_qty;
            }

            // Reset cover leg state
            order_data.leg4_filled = false;
            order_data.leg4_counter = 0;
            order_data.leg4_depth = 0;
            order_data.leg4_ack = true;
            order_data.last_leg4_price = 0;
            order_data.leg4_covered_qty -= order_data.leg4_pending_qty;
            order_data.leg4_pending_qty = 0;
            order_data.last_leg4_qty = order_data.leg4_filled_qty;
            order_data.leg4_filled_qty = 0;
            order_data.leg4_price = 0;
            order_data.last_covered_leg4 = order_data.leg4_covered_qty;

            LOG_TEST("Leg4 order cancelled: qty=" << required_qty);
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in cancel: " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER","Unknown strategy order id received in cancel: " + std::to_string( strategy_order_id));
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleOrderCancel(strat, strategy_order_id);
        break;
    }

    default:
        break;
    }
}


ALWAYS_INLINE void handleModifyReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC: // NOT GOING TO COME FOR THIS STRATEGY
    {

        LOG_LIVE("TEMPLATE_HELPER", "Modify Reject Came For CONREV_IOC which is wrong.");

        break;
    }
    case StrategyKind::CONREV_BID:
    {
        auto &order_data = strat.order_data.three_leg_bidding;

        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            /// DO NOTHING CONTINUE TO MODIFY ON NEXT ITERATIon
            order_data.leg1_ack = true; // as modify reject we should know its rejected
            order_data.leg1_price = order_data.last_price_leg1;
            order_data.leg1_pending_qty = order_data.last_qty_leg1;
            LOG_FILE("TEMPLATE_HELPER", "Leg1 (Future) Modify Reject:");
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {

            order_data.leg2_ack = true;
            order_data.last_leg2_qty = order_data.last_qty_leg2;
            order_data.leg2_pending_qty = order_data.last_qty_leg2;
            order_data.leg2_covered_qty = order_data.last_covered_leg2;

            LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  Modify Reject:");
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.last_leg3_qty = order_data.last_qty_leg3;
            order_data.leg3_pending_qty = order_data.last_qty_leg3;
            order_data.leg3_covered_qty = order_data.last_covered_leg3;

            LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) Modify Reject:");
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in Modiy reject: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in Modiy reject: " + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in Modiy reject: " + std::to_string(strategy_order_id));
        }

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        LOG_FILE("TEMPALTE", "strategy id:" + std::to_string(strategy_order_id) + ", leg1 order id:" + std::to_string(order_data.leg1_order_id) + ", leg2 order id:" + std::to_string(order_data.leg2_order_id) + ", leg3 order id:" + std::to_string(order_data.leg3_order_id) + ", leg4 order id:" + std::to_string(order_data.leg4_order_id));
        if (strategy_order_id == order_data.leg1_order_id)
        {
            /// DO NOTHING CONTINUE TO MODIFY ON NEXT ITERATIon
            order_data.leg1_price = order_data.last_leg1_price;
            order_data.leg1_ack = true;
            order_data.leg1_pending_qty = required_qty - order_data.leg1_filled_qty;
            order_data.last_leg1_qty = required_qty;
            LOG_FILE("TEMPLATE_HELPER", "Leg1 Modify Reject:");
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_price = order_data.last_leg2_price;
            order_data.leg2_ack = true;
            order_data.leg2_pending_qty = required_qty - order_data.leg2_filled_qty;
            order_data.leg2_covered_qty = order_data.last_covered_leg2;
            order_data.last_leg2_qty = required_qty;

            LOG_FILE("TEMPLATE_HELPER", "Leg2 Modify Reject:");
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.leg3_price = order_data.last_leg3_price;
            order_data.leg3_pending_qty = required_qty - order_data.leg3_filled_qty;
            order_data.leg3_covered_qty = order_data.last_covered_leg3;
            order_data.last_leg3_qty = required_qty;

            LOG_FILE("TEMPLATE_HELPER", "Leg3 Modify Reject:");
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            order_data.leg4_price = order_data.last_leg4_price;
            order_data.leg4_ack = true;
            order_data.leg4_pending_qty = required_qty - order_data.leg4_filled_qty;
            order_data.leg4_covered_qty = order_data.last_covered_leg4;
            order_data.last_leg4_qty = required_qty;

            LOG_FILE("TEMPLATE_HELPER", "Leg4 Modify Reject:");
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in ModifyReject: " << strategy_order_id);
            LOG_FILE("TEMPLATE_HELPER", "Unknown strategy order id received in ModifyReject:" + std::to_string(strategy_order_id));
            LOG_LIVE("TEMPLATE_HELPER", "Unknown strategy order id received in ModifyReject:" + std::to_string(strategy_order_id));
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleModifyReject(strat, strategy_order_id);
        break;
    }
    default:
        break;
    }
}

ALWAYS_INLINE void handleCancelReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC: // IMP never occur this case
    {
        auto &order_data = strat.order_data.conrev;
        order_data.ordered_qty = order_data.ordered_qty - required_qty;
        strat.is_iter_over = true; // TODO: iter not
        if (!strat.stop_requested)
        {
            strat.is_active = true;
        }

        break;
    }
    case StrategyKind::CONREV_BID:
    {
        auto &order_data = strat.order_data.three_leg_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            // Leg 1 (Future) order cancelled

            order_data.leg1_cancel_request = false;

            LOG_TEST("Leg1 (Future) order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {

            LOG_TEST("Leg2 (Call) order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {

            LOG_TEST("Leg3 (Put) order cancelled: qty=" << required_qty);
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in cancel Reject: " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER" ,"Unknown strategy order id received in cancel Reject:  "+ std::to_string( strategy_order_id));
        }

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            order_data.leg1_ack = true;
            order_data.leg1_pending_qty = required_qty - order_data.leg1_filled_qty;
            LOG_TEST("Leg1 order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_ack = true;
            order_data.leg2_pending_qty = required_qty - order_data.leg2_filled_qty;
            order_data.leg2_covered_qty = order_data.last_covered_leg2;
            LOG_TEST("Leg2 order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_ack = true;
            order_data.leg3_pending_qty = required_qty - order_data.leg3_filled_qty;
            order_data.leg3_covered_qty = order_data.last_covered_leg3;
            LOG_TEST("Leg3 order cancelled: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg4_order_id)
        {
            order_data.leg4_ack = true;
            order_data.leg4_pending_qty = required_qty - order_data.leg4_filled_qty;
            order_data.leg4_covered_qty = order_data.last_covered_leg4;
            LOG_TEST("Leg4 order cancelled: qty=" << required_qty);
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in cancel Reject:  " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER" ,"Unknown strategy order id received in cancel Reject:  "+ std::to_string( strategy_order_id));
        }

        break;
    }
    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleCancelReject(strat, strategy_order_id);
        break;
    }
    default:
        break;
    }
}

ALWAYS_INLINE void handleRequestFailed(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty, uint32_t fill_qty_sum, OrderManager &order_manager, unsigned long long exe_time) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC:
    {
        strat.is_active = false;
        auto &order_data = strat.order_data.conrev;

        uint8_t leg = order_data.order_id_to_leg.lookup(strategy_order_id);
        switch (leg)
        {
        case 0:
        {
            LOG_FILE("CONREV_IOC", "order_data.ordered_qty before: " + std::to_string(order_data.ordered_qty));
            if (required_qty > fill_qty_sum)
            {
                order_data.ordered_qty = order_data.ordered_qty - (required_qty - fill_qty_sum);
            }

            LOG_FILE("CONREV_IOC", "order_data.ordered_qty after: " + std::to_string(order_data.ordered_qty));

            /* fut */ break;
        }
        case 1:
        {
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v2_call += total_value;
            // order_data.q2_call += fill_qty;

            // LOG_TEST("Leg2 (Call) filled completely: qty=" << fill_qty << " price=" << fill_price);
            // LOG_FILE("TEMPLATE_HELPER", "Leg2 (Call)  filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* call */ break;
        }

        case 2:
        {
            // Update PnL tracking
            // const uint64_t total_value = static_cast<uint64_t>(fill_qty) * fill_price;
            // order_data.v3_put += total_value;
            // order_data.q3_put += fill_qty;

            // LOG_TEST("Leg3 (Put) filled completely: qty=" << fill_qty << " price=" << fill_price);
            // LOG_FILE("TEMPLATE_HELPER", "Leg3 (Put) filled completely: qty=" + std::to_string(fill_qty) + " price=" + std::to_string(fill_price));
            /* put */ break;
        }
        default:
            LOG_FILE("CONREV_IOC", "Unknown or expired order id: " + std::to_string(strategy_order_id));
            break;
        }

        strat.is_iter_over = true;
        if (!strat.stop_requested)
        {
            strat.is_active = true;
        }

        break;
    }

    case StrategyKind::CONREV_BID:
    {
        // auto &order_data = strat.order_data.three_leg_bidding;

        auto &order_data = strat.order_data.three_leg_bidding;

        if (strategy_order_id == order_data.leg1_order_id)
        {
            // Leg 1 (Future) order cancelled

            order_data.leg1_cancel_request = false;
            order_data.leg1_ack = true;
            order_data.leg1_order_id = 0;
            strat.is_iter_over = true;

            LOG_TEST("Leg1 (Future) order request failed: qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg2_order_id)
        {
            order_data.leg2_order_id = 0;
            order_data.leg2_ack = true;
            LOG_TEST("Leg2 (Call) order request failed:  qty=" << required_qty);
        }
        else if (strategy_order_id == order_data.leg3_order_id)
        {
            order_data.leg3_order_id = 0;
            order_data.leg3_ack = true;
            LOG_TEST("Leg3 (Put) order request failed:  qty=" << required_qty);
        }
        else
        {
            LOG_TEST("Unknown strategy order id received in Request Failed: " << strategy_order_id);
            LOG_LIVE("TEMPLATE_HELPER" ,"Unknown strategy order id received in Request Failed: "+ std::to_string( strategy_order_id));

        }

        order_data.state = ThreeLegBiddingState::EXIT;
        strat.stop_reason = UpdateReason::RequestFailed;
        StrategyExecutor<StrategyKind::CONREV_BID>::run(strat, order_data.last_market_snapshot, order_manager, exe_time);

        break;
    }

    case StrategyKind::BOX_1_1_1_1:
    {
        auto &order_data = strat.order_data.box_bidding;

        LOG_FILE("TemplateHelper", "STOP - REQUEST FAILED");

        // order_data.state = FourLegBiddingState1::COMPLETED;
        if (strategy_order_id == order_data.leg1_order_id)
        {
            strat.stop_requested = true;
            // strat.terminate = true;
            strat.is_iter_over = true;
            order_data.state = BoxBiddingStates::COMPLETED;
            StrategyExecutor<StrategyKind::BOX_1_1_1_1>::run(strat, order_data.snap, order_manager, exe_time);
        }
        break;
    }

    case StrategyKind::BOX_2_1_1:
    {
        StrategyExecutor<StrategyKind::BOX_2_1_1>::handleOrderFailed(strat, strategy_order_id);
        break;
    }

    default:
        break;
    }
}

ALWAYS_INLINE bool isStrategyComplete(Portfolio &strat) noexcept
{
    switch (strat.kind)
    {
    case StrategyKind::CONREV_IOC:
    {
        //         (ENTRY IN Strategy order_data.traded_qty , params.max_lots): 700,700
        // (order_data.traded_qty , params.max_lots): 700,700
        // (strat.stop_requested , strat.is_iter_over): 0,1
        // (order_data.traded_qty , params.max_lots): 700,700
        const auto &order_data = strat.order_data.conrev;
        const auto &params = strat.params.conrev;
        LOG_FILE("TEMPLATE_HELPER", "(ENTRY IN Strategy order_data.traded_qty , params.max_lots): " + std::to_string(strat.traded_qty) + "," + std::to_string(params.max_lots));
        // if ((strat.traded_qty >= params.max_lots)  || (strat.stop_requested && strat.pending_orders == 0 && strat.is_iter_over))
        if ((strat.traded_qty >= params.max_lots) || (strat.stop_requested && strat.is_iter_over))
        {
            LOG_FILE("TEMPLATE_HELPER", "(strat.traded_qty , params.max_lots): " + std::to_string(strat.traded_qty) + "," + std::to_string(params.max_lots));
            LOG_FILE("TEMPLATE_HELPER", "(strat.stop_requested , strat.is_iter_over): " + std::to_string(strat.stop_requested) + "," + std::to_string(strat.is_iter_over));

            strat.is_active = false;

            if (strat.traded_qty >= params.max_lots)
            {
                LOG_FILE("TEMPLATE_HELPER", "(strat.traded_qty , params.max_lots): " + std::to_string(strat.traded_qty) + "," + std::to_string(params.max_lots));

                // RESETING FOR RERUN
                // strat.order_data.conrev.achieved_spread = 0;
                // strat.order_data.conrev.leg1_filled = 0;
                // strat.order_data.conrev.leg1_oms_id = 0;
                // strat.order_data.conrev.ordered_qty = 0;
                // strat.order_data.conrev.traded_qty = 0;
                // strat.order_data.conrev.remain_qty = 0;
                strat.terminate = true;
                return true;
                // strat.is_active = false;
            }
            return true;
        }
        return false;
    }
    case StrategyKind::CONREV_BID:
    {
        // LOG_TEST("isStrategyCPple");

        const auto &order_data = strat.order_data.three_leg_bidding;
        const auto &params = strat.params.three_leg_bidding;

        // Strategy is complete if:
        // 1. Max lots reached, OR
        // 2. Stop requested and no pending orders and iteration is over
        // bool max_lots_reached = (order_data.traded_qty >= params.max_lots); as we have resetit in final step dont get it ccorrect

        bool is_terminated = strat.terminate;
        // LOG_TEST("isStrategyCPple  is terminated: " <<is_terminated);

        bool stop_completed = (strat.stop_requested && strat.is_iter_over);

        if (is_terminated || stop_completed)
        {
            LOG_FILE("TEMPLATE_HELPER", " Definetly stopping");

            strat.is_active = false;

            if (is_terminated)
            {
                // Mark strategy as terminated and inactive
                LOG_FILE("TEMPLATE_HELPER", "3LegBidding straegy completed - max lots reached: ");
            }
            else
            {
                LOG_FILE("TEMPLATE_HELPER", "3LegBidding strategy stopped by request");
            }

            return true;
        }

        return false;
    }

    case StrategyKind::BOX_2_1_1:
    {
        LOG_FILE("TEMPATE_HELPER", "Came here is strategy cm");
        return StrategyExecutor<StrategyKind::BOX_2_1_1>::handleStrategyComplete(strat);
    }
    case StrategyKind::BOX_1_1_1_1:
    {
        const auto &order_data = strat.order_data.box_bidding;
        const auto &params = strat.params.box_bidding;

        // Strategy is complete if:
        // 1. Max lots reached, OR
        // 2. Stop requested and no pending orders and iteration is over
        // 3. Mod max or new max limit reached
        // 4. request failed came for leg1.
        // 5. request reject came for leg1.
        // 6. rms reject came for leg1.

        bool is_terminated = strat.terminate;

        bool stop_completed = (strat.stop_requested && strat.is_iter_over);

        if (is_terminated || stop_completed)
        {
            LOG_FILE("TEMPLATE_HELPER", " Definetly stopping");

            strat.is_active = false; // Line added for new code
            // Remove from active portfolios list
            /*for (uint16_t i = 0; i < active_portfolio_count; ++i)
            {
                if (active_portfolios[i] == strat.portfolio_id)
                {
                    active_portfolios[i] = active_portfolios[--active_portfolio_count];
                    break;
                }
            }*/

            if (is_terminated)
            {
                // Mark strategy as terminated and inactive
                LOG_FILE("TEMPLATE_HELPER", "BoxBidding straegy completed - max lots reached: ");
            }
            else
            {
                LOG_FILE("TEMPLATE_HELPER", "BoxBidding strategy stopped by request - due to breach of max limit of mod/new orders or some service went down or user request from frontend");
            }

            LOG_FILE("TemplateHelper", "Ending isStrategyComplete from BoxBidding");
            return true;
        }

        return false;
    }
    default:
        return false;
    }
}

#pragma once
#include "TemplateStrategy.h"
// ConRev specialization
template <>
struct StrategyExecutor<StrategyKind::CONREV_IOC>
{
    static bool run(Portfolio &p,
                    const StrategyMarketSnapshot &s,
                    OrderManager &o,
                    const unsigned long long exe_time) noexcept
    {

        auto &params = p.params.conrev;
        auto &order_data = p.order_data.conrev;

        if (__builtin_expect(order_data.traded_qty >= params.max_lots, 0))
        {
            LOG_FILE("CONREV", "traded greater than or equal max lots:" + std::to_string(order_data.traded_qty) + "," + std::to_string(params.max_lots));
            p.is_active = 0;
            p.terminate = 1;
            return false;
        }
        else if (__builtin_expect(order_data.ordered_qty >= params.max_lots, 0))
        {
            LOG_FILE("CONREV", "ordered greater than max lots ordered :" + std::to_string(order_data.ordered_qty) + ", Max lots:" + std::to_string(params.max_lots));

            LOG_LIVE("CONREV", "Ordered greater than or equal to max lots");
            // p.is_active = 0;
            return false;
        }

        uint32_t fut_bid = s.data.conrev.fut.bids[0], fut_ask = s.data.conrev.fut.asks[0];
        uint32_t call_bid = s.data.conrev.call.bids[0], call_ask = s.data.conrev.call.asks[0];
        uint32_t put_bid = s.data.conrev.put.bids[0], put_ask = s.data.conrev.put.asks[0];
        uint32_t strike = p.legs[1].strike_price;
        uint32_t current_sol = params.sol > params.max_lots - order_data.ordered_qty ? params.max_lots - order_data.ordered_qty : params.sol; // 1500 1500  2250-1500=750

        int64_t spread = 0;
        bool ok = false;
        // LOG_TEST("Placing order");
        order_data.con_flag = params.con_flag;
        if (params.con_flag) [[likely]]
        {

            if (fut_ask == 0 || call_bid == 0 || put_ask == 0)
                return false;
            // order_data.con_flag = true; // TODO: all setting may gone as params has conflag
            spread = int64_t(strike) - int64_t(fut_ask) +
                     int64_t(call_bid) - int64_t(put_ask);

            if (spread >= int64_t(params.spread)) [[likely]]
            {
                // LOG_COUT("current spread:" << spread << ", Given spread forward :"<<params.forward_spread<< ":Strike price :"<<strike <<" fut_ask:"<<fut_ask<<" call_bid:"<<call_bid<<" put_ask:"<<put_ask);
                Leg fut{p.legs[0].symbol_token, fut_ask, current_sol, Side::Buy, s.data.conrev.fut.start_time};
                Leg call{p.legs[1].symbol_token, call_bid, current_sol, Side::Sell, s.data.conrev.call.start_time};
                Leg put{p.legs[2].symbol_token, put_ask, current_sol, Side::Buy, s.data.conrev.put.start_time};
                p.is_iter_over = false;

                ok = o.sendThreeLegOrder(p.portfolio_id, order_data.leg1_order_id, order_data.leg2_order_id, order_data.leg3_order_id, OrderType::IOC, fut, call, put, exe_time, false);
                order_data.order_id_to_leg.insert(order_data.leg1_order_id, 0);
                order_data.order_id_to_leg.insert(order_data.leg2_order_id, 1);
                order_data.order_id_to_leg.insert(order_data.leg3_order_id, 2);
            }
        }
        else
        {

            if (fut_bid == 0 || call_ask == 0 || put_bid == 0)
                return false;

            // order_data.con_flag = false;
            spread = -int64_t(strike) + int64_t(fut_bid) -
                     int64_t(call_ask) + int64_t(put_bid);

            if (spread >= int64_t(params.spread)) [[likely]]
            {
                // LOG_COUT("current spread:" << spread << ", Given spread revers :"<<params.revers_spread);
                Leg fut{p.legs[0].symbol_token, fut_bid, current_sol, Side::Sell, s.data.conrev.fut.start_time};
                Leg call{p.legs[1].symbol_token, call_ask, current_sol, Side::Buy, s.data.conrev.call.start_time};
                Leg put{p.legs[2].symbol_token, put_bid, current_sol, Side::Sell, s.data.conrev.put.start_time};
                p.is_iter_over = false;

                ok = o.sendThreeLegOrder(p.portfolio_id, order_data.leg1_order_id, order_data.leg2_order_id, order_data.leg3_order_id, OrderType::IOC, fut, call, put, exe_time, false);
                order_data.order_id_to_leg.insert(order_data.leg1_order_id, 0);
                order_data.order_id_to_leg.insert(order_data.leg2_order_id, 1);
                order_data.order_id_to_leg.insert(order_data.leg3_order_id, 2);
            }
        }

        if (__builtin_expect(ok, 1))
        {
            order_data.ordered_qty += current_sol;
            order_data.remain_qty = params.max_lots - order_data.ordered_qty;
            return true;
        }
        else
        {
            LOG_FILE("CONREV", "Order Not sended");
        }

        return false;
    }
};

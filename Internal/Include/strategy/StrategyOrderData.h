#pragma once
#include <cstdint>
#include <cstring>
#include "StrategyEnums.h"
#include "../orders/OrderTracking.h"
#include "../market/MarketSnapshot.h"

#pragma pack(push, 1)

struct ConRevOrderData
{
    uint16_t portfolio_id;
    uint32_t traded_qty = 0;
    uint32_t ordered_qty = 0;
    uint32_t remain_qty = 0;
    int32_t achieved_spread = 0;
    uint32_t leg1_oms_id = 0;
    bool leg1_filled = false;

    uint32_t leg1_order_id = 0;
    uint32_t leg2_order_id = 0;
    uint32_t leg3_order_id = 0;
    OrderIDLegMapRing order_id_to_leg;

    uint32_t fut_token = 0;
    uint32_t call_token = 0;
    uint32_t put_token = 0;

    uint32_t fut_price = 0;
    uint32_t call_price = 0;
    uint32_t put_price = 0;

    uint32_t get_price_by_token(uint32_t token) const
    {
        if (token == fut_token) return fut_price;
        if (token == call_token) return call_price;
        if (token == put_token) return put_price;
        return 0;
    }

    void set_price_by_token(uint32_t token, uint32_t price)
    {
        if (token == fut_token) fut_price = price;
        else if (token == call_token) call_price = price;
        else if (token == put_token) put_price = price;
    }

    uint64_t v1_fut = 0;
    uint64_t v2_call = 0;
    uint64_t v3_put = 0;
    uint32_t q1_fut = 0;
    uint32_t q2_call = 0;
    uint32_t q3_put = 0;

    uint32_t price_fut = 0;
    uint32_t price_call = 0;
    uint32_t price_put = 0;

    int32_t strike_price = 0;
    bool con_flag = true;
};

struct ThreeLegBiddingOrderData
{
    uint16_t portfolio_id;
    uint32_t leg1_over_all_fill = 0;
    uint32_t leg2_over_all_fill = 0;
    uint32_t leg3_over_all_fill = 0;

    StrategyMarketSnapshot last_market_snapshot;
    uint32_t leg2_covered_qty = 0;
    uint32_t leg3_covered_qty = 0;
    uint32_t leg2_pending_qty = 0;
    uint32_t leg3_pending_qty = 0;

    ThreeLegBiddingState state = ThreeLegBiddingState::IDLE;
    bool is_conversion = false;

    uint32_t last_leg1_price = 0;
    uint32_t last_qty_leg1;
    uint32_t last_qty_leg2;
    uint32_t last_covered_leg2;
    uint32_t last_qty_leg3;
    uint32_t last_covered_leg3;

    uint32_t last_price_leg1;
    uint32_t last_price_leg2;
    uint32_t last_price_leg3;

    uint32_t leg1_order_id = 0;
    uint32_t leg2_order_id = 0;
    uint32_t leg3_order_id = 0;

    bool leg1_ack = false;
    bool leg2_ack = false;
    bool leg3_ack = false;
    bool leg1_cancel_request = false;

    uint32_t traded_qty = 0;
    uint32_t current_cycle_qty = 0;
    uint32_t current_hedge_qty = 0;

    bool leg1_filled = false;
    bool leg1_partial_filled = false;
    uint32_t leg1_pending_qty = 0;
    uint32_t leg1_filled_qty = 0;
    uint32_t leg1_price = 0;
    uint64_t leg1_timer_start = 0;

    bool leg2_filled = false;
    uint32_t leg2_filled_qty = 0;
    uint32_t leg2_counter = 0;
    uint32_t leg2_price = 0;
    uint32_t leg2_depth = 0;
    uint32_t last_leg2_price = 0;
    uint32_t last_leg2_qty = 0;
    uint64_t legs2_timer_start = 0;
    uint32_t legs2_level = 1;
    bool leg2_level2 = false;
    uint32_t leg2_actual_fill_qty = 0;
    bool leg2_ack_came;
    uint32_t leg2_last_qty = 0;

    bool leg3_filled = false;
    uint32_t leg3_filled_qty = 0;
    uint32_t leg3_price = 0;
    uint32_t leg3_counter = 0;
    uint32_t leg3_depth = 0;
    uint32_t last_leg3_price = 0;
    uint32_t last_leg3_qty = 0;
    uint64_t legs3_timer_start = 0;
    uint32_t legs3_level = 1;
    bool leg3_level2 = false;
    uint32_t leg3_actual_fill_qty = 0;
    bool leg3_ack_came;
    uint32_t leg3_last_qty = 0;

    uint64_t v1_fut = 0;
    uint64_t v2_call = 0;
    uint64_t v3_put = 0;
    uint32_t q1_fut = 0;
    uint32_t q2_call = 0;
    uint32_t q3_put = 0;

    uint32_t price_fut = 0;
    uint32_t price_call = 0;
    uint32_t price_put = 0;
    uint32_t strike_price = 0;
    int64_t achieved_spread = 0;

    uint32_t bid_traded_qty = 0;
    uint32_t bid_ordered_qty = 0;
    uint32_t new_max = 0;
    uint32_t mod_max = 0;
    uint32_t limit_new_max = 0;
    uint32_t limit_mod_max = 0;
    uint32_t current_cycle_traded = 0;
};

struct BoxBiddingOrderData
{
    uint16_t portfolio_id;

    uint32_t itm_call_token = 0;
    uint32_t itm_put_token = 0;
    uint32_t otm_call_token = 0;
    uint32_t otm_put_token = 0;

    uint32_t leg2_covered_qty = 0;
    uint32_t leg3_covered_qty = 0;
    uint32_t leg4_covered_qty = 0;
    uint32_t leg2_pending_qty = 0;
    uint32_t leg3_pending_qty = 0;
    uint32_t leg4_pending_qty = 0;

    StrategyMarketSnapshot snap;
    uint8_t entry_leg = 0;

    BoxBiddingStates state = BoxBiddingStates::IDLE;
    bool is_flip = false;

    uint32_t leg1_order_id = 0;
    uint32_t leg2_order_id = 0;
    uint32_t leg3_order_id = 0;
    uint32_t leg4_order_id = 0;

    BoxBiddingLegs leg1;
    BoxBiddingLegs leg2;
    BoxBiddingLegs leg3;
    BoxBiddingLegs leg4;

    bool leg1_ack = true;
    bool leg2_ack = true;
    bool leg3_ack = true;
    bool leg4_ack = true;

    uint32_t traded_qty = 0;
    uint32_t current_cycle_qty = 0;
    uint32_t current_hedge_qty = 0;

    bool leg1_filled = false;
    bool leg1_partial_filled = false;
    uint32_t leg1_pending_qty = 0;
    uint32_t leg1_filled_qty = 0;
    uint32_t leg1_price = 0;
    uint64_t leg1_timer_start = 0;
    uint32_t last_leg1_qty = 0;
    uint32_t last_leg1_price = 0;

    bool leg2_filled = false;
    uint32_t leg2_filled_qty = 0;
    uint32_t leg2_counter = 0;
    uint32_t leg2_depth = 0;
    uint32_t last_leg2_price = 0;
    uint32_t leg2_price = 0;
    uint32_t last_leg2_qty = 0;
    uint32_t last_covered_leg2 = 0;
    uint64_t legs2_timer_start = 0;
    uint32_t legs2_level = 1;
    bool leg2_level2 = false;
    uint32_t leg2_actual_fill_qty = 0;

    bool leg3_filled = false;
    uint32_t leg3_filled_qty = 0;
    uint32_t leg3_counter = 0;
    uint32_t leg3_depth = 0;
    uint32_t last_leg3_price = 0;
    uint32_t leg3_price = 0;
    uint32_t last_leg3_qty = 0;
    uint32_t last_covered_leg3 = 0;
    uint64_t legs3_timer_start = 0;
    uint32_t legs3_level = 1;
    bool leg3_level2 = false;
    uint32_t leg3_actual_fill_qty = 0;

    bool leg4_filled = false;
    uint32_t leg4_filled_qty = 0;
    uint32_t leg4_counter = 0;
    uint32_t leg4_depth = 0;
    uint32_t last_leg4_price = 0;
    uint32_t leg4_price = 0;
    uint32_t last_leg4_qty = 0;
    uint32_t last_covered_leg4 = 0;
    uint64_t legs4_timer_start = 0;
    uint32_t legs4_level = 1;
    bool leg4_level2 = false;
    uint32_t leg4_actual_fill_qty = 0;

    uint32_t v1 = 0;
    uint32_t q1 = 0;
    uint32_t v2 = 0;
    uint32_t q2 = 0;
    uint32_t v3 = 0;
    uint32_t q3 = 0;
    uint32_t v4 = 0;
    uint32_t q4 = 0;

    uint32_t price_leg1_for_entire_cycle = 0;
    uint32_t price_leg2_for_entire_cycle = 0;
    uint32_t price_leg3_for_entire_cycle = 0;
    uint32_t price_leg4_for_entire_cycle = 0;
    uint32_t strike_diff = 0;
    int64_t achieved_spread = 0;

    uint32_t bid_traded_qty = 0;
    uint32_t bid_ordered_qty = 0;
    uint32_t new_max = 0;
    uint32_t mod_max = 0;

    int32_t modify_reject = 0;
    int32_t cancel_reject = 0;
};

struct BoxIocOrderData
{
    uint16_t portfolio_id;
    BoxIocStrategyState state;
    bool is_flip_box;

    uint32_t call_itm_order_id;
    uint32_t put_itm_order_id;
    uint32_t call_itm_pending_qty;
    uint32_t put_itm_pending_qty;
    uint32_t call_itm_filled_qty;
    uint32_t put_itm_filled_qty;
    bool call_itm_ack;
    bool put_itm_ack;
    bool call_itm_cancelled;
    bool put_itm_cancelled;
    bool call_itm_filled;
    bool put_itm_filled;

    uint32_t second_leg_order_id;
    uint32_t second_leg_pending_qty;
    uint32_t second_leg_last_pending_qty;
    uint32_t second_leg_current_fill;
    uint32_t second_leg_filled_qty;
    uint32_t second_leg_price;
    uint32_t second_leg_last_price;
    uint32_t second_leg_depth;
    uint32_t second_leg_counter;
    bool second_leg_ack;
    bool second_leg_timer_identifier;
    uint32_t last_quantity_second_leg;

    uint32_t third_leg_order_id;
    uint32_t third_leg_pending_qty;
    uint32_t third_leg_last_pending_qty;
    uint32_t third_leg_current_fill;
    uint32_t third_leg_filled_qty;
    uint32_t third_leg_price;
    uint32_t third_leg_last_price;
    uint32_t third_leg_depth;
    uint32_t third_leg_counter;
    bool third_leg_ack;
    bool third_leg_timer_identifier;
    uint32_t last_quantity_third_leg;

    uint64_t itm_timer_start;
    uint64_t otm_timer_start;
    uint64_t second_otm_timer_start;
    uint64_t third_otm_timer_start;
    uint32_t timer_value;

    int64_t strike_difference;
    int64_t current_spread;
    uint32_t traded_qty;

    bool first_leg_trade;
    bool second_leg_trade;
    bool place_second_order;
    bool place_aggressive;
    bool modify_flag;
    bool terminate_check;

    BoxIocOrderData() : state(BoxIocStrategyState::IDLE),
                        is_flip_box(false),
                        call_itm_pending_qty(0),
                        put_itm_pending_qty(0),
                        call_itm_cancelled(false),
                        put_itm_cancelled(false),
                        second_leg_order_id(0),
                        second_leg_pending_qty(0),
                        second_leg_filled_qty(0),
                        second_leg_price(0),
                        second_leg_depth(0),
                        second_leg_counter(0),
                        second_leg_ack(true),
                        second_leg_timer_identifier(false),
                        second_leg_current_fill(0),
                        last_quantity_second_leg(0),
                        third_leg_order_id(0),
                        third_leg_pending_qty(0),
                        third_leg_filled_qty(0),
                        third_leg_price(0),
                        third_leg_depth(0),
                        third_leg_counter(0),
                        third_leg_ack(true),
                        third_leg_timer_identifier(false),
                        third_leg_current_fill(0),
                        last_quantity_third_leg(0),
                        itm_timer_start(0),
                        otm_timer_start(0),
                        timer_value(0),
                        strike_difference(0),
                        traded_qty(0),
                        current_spread(0),
                        first_leg_trade(false),
                        second_leg_trade(false),
                        place_second_order(true),
                        place_aggressive(false),
                        modify_flag(false),
                        terminate_check(false) {}
};

union AlgorithmOrderData
{
    ConRevOrderData conrev;
    ThreeLegBiddingOrderData three_leg_bidding;
    BoxBiddingOrderData box_bidding;
    BoxIocOrderData box_ioc;

    AlgorithmOrderData() { memset(this, 0, sizeof(*this)); }
    ~AlgorithmOrderData() {}
};

#pragma pack(pop)
#pragma once
#include <cstdint>
#include <cstring>
#include "StrategyEnums.h"

#pragma pack(push, 1)

struct ConRevParams
{
    uint16_t portfolio_id;
    bool con_flag;
    int32_t max_lots;
    int32_t sol;
    double spread;
    int opp_check;
    double diff;
    double timer;
};

struct ThreeLegBiddingParams
{
    uint16_t portfolio_id;
    bool con_flag;
    int32_t max_lots;
    int32_t sol;
    double spread;
    int32_t leg1_spread_threshold;
    uint64_t legs2_timeout_us;
    uint64_t legs3_timeout_us;
    bool is_opportunity = false;
};

struct BoxBiddingParams
{
    uint16_t portfolio_id;
    uint32_t max_lots;
    uint32_t sol;
    bool flip_box_enabled;
    int64_t price_difference;
    int64_t flip_price_difference;
    int32_t leg1_spread_threshold;
    uint64_t legs2_timeout_us;
    uint64_t legs3_timeout_us;
    uint64_t legs4_timeout_us;
    uint64_t leg1_timeout_us;
    uint8_t entry_leg = 0;
    bool is_opportunity = false;
};

struct BoxIocParams
{
    uint16_t portfolio_id;
    uint32_t call_itm_token;
    uint32_t put_otm_token;
    uint32_t call_otm_token;
    uint32_t put_itm_token;
    int64_t price_difference;
    bool is_flip_box;
    uint32_t max_lots;
    uint32_t sol;
    uint32_t timer_ms;

    BoxIocParams() : call_itm_token(0),
                     put_otm_token(0),
                     call_otm_token(0),
                     put_itm_token(0),
                     price_difference(0),
                     max_lots(0),
                     sol(0),
                     timer_ms(100),
                     is_flip_box(false) {}
};


struct BascketData
{
    uint32_t fno_token;
    uint32_t cash_token;
    uint32_t fno_fill_qty;
    uint8_t fno_stream;
    uint8_t cash_stream;
};

union AlgorithmParams
{
    ConRevParams conrev;
    ThreeLegBiddingParams three_leg_bidding;
    BoxBiddingParams box_bidding;
    BoxIocParams box_ioc;

    AlgorithmParams() { memset(this, 0, sizeof(*this)); }
};

struct StrategyParamsFrontend
{
    StrategyKind kind;

    union
    {
        struct
        {
            bool con_flag;
            int32_t max_lots;
            int32_t sol;
            double spread;
        } CONREV_IOC;

        struct
        {
            bool con_flag;
            int32_t max_lots;
            int32_t sol;
            double spread;
            int opp_check;
            double diff;
            double timer;
        } CONREV_BID;

        struct {
            uint32_t maxLots;
            uint32_t sol;
            double minSpread;           // Scaled by 10000
            bool isCon;
            uint32_t indexToken;
            uint64_t indexTargetExpiry;
            uint64_t stockTargetExpiry;
            uint32_t tokensCount;
            BascketData basket_tokens_info [51];
            double levelEscalationTimeMs;  // Scaled from microseconds
        } INDEX_ARB;

        struct
        {
            uint32_t max_lots;
            uint32_t sol;
            bool flipBoxEnabled;
            double priceDifference;
            double coverLegsTimeoutUs;
        } BOX_2_1_1;

        struct
        {
            uint32_t maxLots;
            uint32_t sol;
            bool flipBoxEnabled;
            double priceDifference;
            uint8_t entry_leg;
            double leg1SpreadThreshold;
            double TimeoutUs;
            bool isOpportunity;
        } BOX_1_1_1_1;
    };
};

#pragma pack(pop)
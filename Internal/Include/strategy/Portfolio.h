#pragma once
#include <cstdint>
#include "StrategyEnums.h"
#include "StrategyParams.h"
#include "StrategyOrderData.h"
#include "../core/BasicTypes.h"
#include "../frontend/FrontendEnums.h"

#pragma pack(push, 1)

struct Portfolio
{
    uint16_t portfolio_id;
    StrategyKind kind;
    uint8_t leg_count;
    LegData legs[4];

    bool is_active;
    bool terminate;
    bool updated_tick;
    bool is_iter_over;
    bool stop_requested;
    StaleStatus stale_status;
    bool ordered_flag;
    bool is_data_updated;
    int32_t achieved_spread = 0;
    uint32_t traded_qty = 0;
    UpdateReason stop_reason;

    AlgorithmParams params;
    AlgorithmOrderData order_data;

    Portfolio() : portfolio_id(0), 
                  kind(StrategyKind::UNKNOWN), 
                  leg_count(0),
                  is_active(false), 
                  terminate(false), 
                  is_iter_over(true),
                  stop_requested(false),
                  stale_status(StaleStatus::NOT_STALE), 
                  stop_reason(UpdateReason::Default) {}
};

struct PortfolioShm
{
    uint16_t portfolio_id;
    StrategyKind kind;
    uint8_t leg_count;
    LegData legs[4];

    StrategyParamsFrontend params;

    bool is_active = true;
    bool terminate = false;
    bool is_iter_over = false;
    bool stop_requested = false;
    StaleStatus stale_status;
    uint32_t traded_qty = 0;
    int32_t achieved_spread = 0;

    PortfolioShm() : portfolio_id(0), 
                     kind(StrategyKind::UNKNOWN), 
                     leg_count(0),
                     is_active(false), 
                     terminate(false), 
                     stale_status(StaleStatus::NOT_STALE) {}
};

#pragma pack(pop)
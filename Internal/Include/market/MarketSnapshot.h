#pragma once
#include "MarketData.h"
#include "../strategy/StrategyEnums.h"

#pragma pack(push, 1)

union StrategyMarketSnapshotData
{
    struct
    {
        StoredMarketDataLatency fut;
        StoredMarketDataLatency call;
        StoredMarketDataLatency put;
    } conrev;

    struct
    {
        StoredMarketDataLatency fut;
        StoredMarketDataLatency call;
        StoredMarketDataLatency put;
    } three_leg_bidding;

    struct
    {
        StoredMarketDataLatency long_call;
        StoredMarketDataLatency short_call;
        StoredMarketDataLatency long_put;
        StoredMarketDataLatency short_put;
    } box_bidding;

    struct
    {
        StoredMarketDataLatency itm_call;
        StoredMarketDataLatency otm_put;
        StoredMarketDataLatency otm_call;
        StoredMarketDataLatency itm_put;
    } box_ioc;

    StrategyMarketSnapshotData() { memset(this, 0, sizeof(*this)); }
};

struct StrategyMarketSnapshot
{
    StrategyKind kind;
    StrategyMarketSnapshotData data;
    uint64_t timestamp;
    bool is_valid;

    StrategyMarketSnapshot() : kind(StrategyKind::UNKNOWN), timestamp(0), is_valid(false) {}
};

#pragma pack(pop)
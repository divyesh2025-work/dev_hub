#pragma once
#include <cstdint>
#include <algorithm>
#include "OrderEnums.h"
#include "../core/BasicTypes.h"
#include "../core/Macros.h"
#include "../sdk/strategy_sdk.h"

#pragma pack(push, 1)

struct StrategyLegData
{
    uint32_t token;
    OrderSide side;
    uint16_t portfolio_id;
    uint32_t fill_price_sum = 0;
    uint32_t fill_qty_sum = 0;
    uint32_t ordered_price = 0;
    uint32_t required_qty = 0;
    uint32_t oms_order_id;
    uint64_t exchange_order_id;
    uint64_t exchange_modified_time;
    OrderState order_state;
};

struct OrderIDLegMapRing
{
    static constexpr size_t Capacity = 128;
    static constexpr uint32_t Mask = Capacity - 1;

    uint8_t leg_index_ring[Capacity];

    OrderIDLegMapRing()
    {
        std::fill(std::begin(leg_index_ring), std::end(leg_index_ring), 255);
    }

    ALWAYS_INLINE void insert(uint32_t strategy_order_id, uint8_t leg_index) noexcept
    {
        leg_index_ring[strategy_order_id & Mask] = leg_index;
    }

    ALWAYS_INLINE uint8_t lookup(uint32_t strategy_order_id) const noexcept
    {
        return leg_index_ring[strategy_order_id & Mask];
    }

    ALWAYS_INLINE void erase(uint32_t strategy_order_id) noexcept
    {
        leg_index_ring[strategy_order_id & Mask] = 255;
    }
};


#pragma pack(pop)
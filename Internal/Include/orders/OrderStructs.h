#pragma once
#include <cstdint>
#include "OrderEnums.h"
#include "../core/BasicTypes.h"
#include "../sdk/strategy_sdk.h"
#pragma pack(push, 1)


struct Trade
{
    uint32_t oms_order_id;
    uint16_t portfolio_id;
    uint32_t fill_qty;
    uint32_t fill_price;
    uint64_t exchange_order_id;
    uint64_t timestamp;
    bool partial_fill;
};

struct alignas(64) OrderMessage
{
    OrderMessageType type;

    union
    {
        struct
        {
            uint16_t portfolio_id;
            OrderType type;
            uint8_t num_legs;
            Leg legs[3];
        } new_order;

        struct
        {
            uint16_t portfolio_id;
            uint8_t num_legs;
            Leg legs[3];
        } ack;

        struct
        {
            uint32_t oms_order_id;
            uint64_t exchange_order_id;
            uint64_t timestamp;
            uint32_t price;
            uint32_t qty;
        } new_ack;

        struct
        {
            uint32_t oms_order_id;
            OrderMessageType update_type;
            uint32_t fill_qty;
            uint32_t fill_price;
            uint64_t exchange_order_id;
            uint64_t timestamp;
        } update;

        struct
        {
            uint32_t oms_order_id;
            uint32_t error_code;
            uint64_t exchange_order_id;
            uint64_t timestamp;
        } reject;

        struct
        {
            uint32_t oms_order_id;
        } cancel;

        struct
        {
            uint32_t oms_order_id;
            uint64_t exchange_order_id;
            uint64_t timestamp;
        } cancel_ack;

        struct
        {
            uint32_t oms_order_id;
            Leg new_leg;
        } modify;

        struct
        {
            uint32_t oms_order_id;
            uint64_t exchange_modified_time;
            uint64_t exchange_order_id;
            uint32_t price;
            uint32_t qty;
        } modify_ack;
    };
};

#pragma pack(pop)
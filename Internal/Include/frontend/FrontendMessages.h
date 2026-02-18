#pragma once
#include <cstdint>
#include "FrontendEnums.h"
#include "../core/BasicTypes.h"
#include "../strategy/StrategyParams.h"
#include "../orders/OrderStructs.h"

#pragma pack(push, 1)

struct FrontendMessage
{
    FrontendMessageType msg_type;
    uint16_t portfolio_id;
    
    union
    {
        struct
        {
            StrategyKind strategy_kind;
            uint8_t leg_count;
            StrategyParamsFrontend params;
            LegData legs[4];
            bool is_active;
            bool terminate;
        } add;

        struct
        {
            bool is_active;
            bool terminate;
            UpdateReason update_reason;
            OrderMessage oms_msg;
        } update;

        struct
        {
            StrategyParamsFrontend params;
        } edit;

        struct
        {
            RequestStatus req_status;
            StatusReason status_reason;
        } ack;

        struct
        {
            uint16_t sid_count;
            uint16_t sids[10];
        } forcestop;

        struct
        {
            size_t recovery_bytes;
        } recovery_ack;

        struct
        {
            StaleStatus status;
        } stale_status_update;
    };
};

#pragma pack(pop)
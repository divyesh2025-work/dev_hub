#pragma once
#include <cstdint>

enum class OrderMessageType : uint8_t
{
    OrderPlacement = 0,
    OrderAck = 1,
    Fill = 2,
    CancelRequest = 3,
    CancelAck = 4,
    ModifyRequest = 5,
    ModifyAck = 6,
    RmsReject = 7,
    ModifyReject = 8,
    CancelReject = 9,
    NewReject = 10,
    PartialFill = 11,
    RequestFailed = 12,
    NewOrderAck = 13,
    ModifySuccess = 14,
    CancelFailed = 15,
    ModifyFailed = 16,
    RecoveryFill = 17,
    RecoveryPartialFill = 18
};

enum class OrderType : uint8_t
{
    Bidding = 0,
    IOC = 1
};

// enum class OrderState : uint8_t
// {
//     NewOms = 0,
//     NewExchange = 1,
//     ModifyOms = 2,
//     ModifyExchange = 3,
//     CancelExchange = 4,
//     ExchnageRejected = 5,
//     Fill = 6,
//     PartialFill = 7
// };
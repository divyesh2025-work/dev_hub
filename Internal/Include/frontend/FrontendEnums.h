#pragma once
#include <cstdint>

enum class FrontendMessageType : uint8_t
{
    Add = 0,
    Run = 1,
    Edit = 2,
    Stop = 3,
    StopAll = 4,
    Remove = 5,
    Pause = 6,
    Status = 7,
    StatusAll = 8,
    AddAck = 9,
    RunAck = 10,
    EditAck = 11,
    StopAck = 12,
    StopAllAck = 13,
    PauseAck = 14,
    RemoveAck = 15,
    Subscribe = 16,
    SubsribeAck = 17,
    OmsUpdate = 18,
    UtradeDisconnected = 19,
    ForceStop = 21,
    ForceStopAck = 22,
    ForceStopAll = 23,
    RecoveryRequest = 24,
    RecoveryRequestAck = 25,
    StaleStatusUpdate = 26
};

enum class RequestStatus : uint8_t
{
    Added,
    NotAdded,
    Edited,
    NotEdited,
    Runned,
    NotRunned,
    Stopped,
    NotStopped,
    Removed,
    NotRemoved,
    StoppedAll,
    NotStoppedAll
};

enum class StatusReason : uint8_t
{
    NotExist,
    IterationOn,
    UtradeDisconnect
};

enum class UpdateReason : uint8_t
{
    Fill,
    PartialFill,
    Completed,
    RMSReject,
    RequestFailed,
    DelayedStopped,
    NewMax,
    ModMax,
    TickStale,
    Default
};
#pragma once
#include <cstdint>

enum class StrategyKind : uint8_t
{
    CONREV_BID,
    CONREV_IOC,
    BOX_2_1_1,
    BOX_1_1_1_1,
    INDEX_ARB,
    UNKNOWN
};

enum class ThreeLegBiddingState : uint8_t
{
    IDLE = 0,
    LEG1_PENDING = 1,
    LEG1_FILLED = 2,
    LEGS23_PENDING = 3,
    LEGS23_LEVEL2 = 4,
    COMPLETED = 5,
    LEG1_PARTIAL_FILLED = 6,
    EXIT = 7
};

enum class BoxBiddingLegs : uint8_t
{
    ITM_CALL,
    ITM_PUT,
    OTM_CALL,
    OTM_PUT
};

enum class BoxBiddingStates : uint8_t
{
    IDLE,
    LEG1_PENDING,
    LEG1_PARTIAL_FILLED,
    LEG1_FILLED,
    LEGS234_PENDING,
    COMPLETED,
    EXIT
};

enum class BoxBiddingState : uint8_t
{
    IDLE = 0,
    MAIN_LEGS_PENDING,
    MAIN_LEGS_FILLED,
    COVER_LEGS_PENDING,
    COVER_LEGS_LEVEL2,
    COMPLETED
};

enum class BoxIocStrategyState : uint8_t
{
    IDLE = 0,
    ITM_PENDING = 1,
    ITM_FILLED = 2,
    OTM_PENDING = 3,
    COMPLETED = 4,
    EXIT = 5
};
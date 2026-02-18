#pragma once
#include <cstdint>

#pragma pack(push, 1)

enum class OrderSide : uint8_t
{
    Buy = 0,
    Sell = 1
};

enum class FDTag
{
    FrontendSocket,
    FrontendClient,
    UTrade,
    Market,
    Subscription
};

enum class StaleStatus : uint8_t
{
    NOT_STALE = 0,
    UTRADE_STALE = 1,
    STRATEGY_STALE = 2,
    RMS_REJECTED = 3,
    FORCE_STOP = 4,
    TICK_STALE =5
};

enum class LegType : uint8_t
{
    Future,
    Call,
    Put
};

struct LegData
{
    uint32_t symbol_token;
    uint8_t stream;
    uint32_t strike_price;
    OrderSide side;
    LegType leg_type;
    uint32_t lot_size;
    bool is_pro_account;
};


struct LogMessageShm
{
    uint8_t msg_type;
    bool is_utrade;
    /* data */
};


#pragma pack(pop)

static constexpr uint64_t MAX_TOKENS = 200000;
static constexpr uint16_t MAX_PORTFOLIOS = 10000;
constexpr size_t MAX_OMS_ORDERS = 65536;
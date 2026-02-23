#ifndef STRATEGY_SDK_H
#define STRATEGY_SDK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════
 * MARKET DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int64_t  price;
    int32_t  qty;
    int32_t  orders;
} __attribute__((packed)) BookLevel;


    // uint32_t bids[5];
    // uint32_t asks[5];
    // uint32_t bids_qty[5];
    // uint32_t asks_qty[5];

    // long start_time;
    // uint32_t seqno;
    // uint32_t internal_seqno;
    // uint32_t last_traded_price;
    // uint8_t stream_id;
    // char msg_type;

typedef struct {
    uint32_t   token;
    uint32_t bids[5];
    uint32_t asks[5];
    uint32_t bids_qty[5];
    uint32_t asks_qty[5];

    long start_time;
    uint32_t seqno;
    uint32_t internal_seqno;
    uint32_t last_traded_price;
    uint8_t stream_id;
    char msg_type;
} __attribute__((packed)) MarketEvent;

/* ═══════════════════════════════════════════════════════════
 * ORDER UPDATE STRUCTURES
 * ═══════════════════════════════════════════════════════════ */

// typedef enum : uint8_t {
//     ORDER_STATE_NEW_OMS = 1,
//     ORDER_STATE_NEW_EXCHANGE = 2,
//     ORDER_STATE_MODIFY_OMS = 3,
//     ORDER_STATE_MODIFY_EXCHANGE = 4,
//     ORDER_STATE_PARTIAL_FILL = 5,
//     ORDER_STATE_FILL = 6,
//     ORDER_STATE_CANCELLED = 7,
//     ORDER_STATE_REJECTED = 8
// } OrderState;

typedef enum : uint8_t {
    NewOms = 0,
    NewExchange = 1,
    ModifyOms = 2,
    ModifyExchange = 3,
    CancelExchange = 4,
    ExchnageRejected = 5,
    Fill = 6,
    PartialFill = 7
} OrderState;

typedef struct {
    uint32_t    oms_order_id;
    uint64_t    exchange_order_id;
    uint32_t    token;
    uint8_t     side;           // 0=buy, 1=sell
    OrderState  state;
    int64_t     ordered_price;
    int32_t     ordered_qty;
    int32_t     filled_qty;
    int64_t     avg_fill_price;
} __attribute__((packed)) OrderUpdate;

typedef struct {
    uint32_t token;
    int32_t  net_qty;
    int64_t  avg_price;
    int64_t  realised_pnl;
} __attribute__((packed)) PositionView;

typedef struct {
    uint32_t    oms_order_id;
    uint32_t    token;
    uint8_t     side;
    int64_t     price;
    int32_t     qty;
    OrderState  state;
} __attribute__((packed)) OpenOrderView;

#pragma pack(push, 1)

struct Leg
{
    uint32_t symbol_id;
    uint32_t price;
    uint32_t qty;
    OrderSide side;
    unsigned long long start_time;
    uint32_t oms_order_id;
};

#pragma pack(pop)

/* ═══════════════════════════════════════════════════════════
 * STRATEGY STATUS UPDATE (strategy → frontend)
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t pf_id;
    int32_t  traded_qty;
    int32_t  achieved_spread;      // Spread when orders were placed
    int32_t  current_spread;       // NEW: Real-time spread from market
    bool     is_complete;
    bool     has_opportunity;      // NEW: Is current spread above threshold?
    uint8_t  custom_data[512];
    uint32_t custom_data_len;
} __attribute__((packed)) StrategyStatusUpdate;

/* ═══════════════════════════════════════════════════════════
 * PLATFORM API (callable from strategy)
 * ═══════════════════════════════════════════════════════════ */

typedef struct PlatformContext PlatformContext;

typedef struct {
    int32_t (*place_new_order_multi_leg)(
        PlatformContext* ctx,
        uint32_t pf_id,
        Leg* legs, 
        uint8_t leg_count, 
        OrderType order_type
    );
    
    int32_t (*place_modify_order)(
        PlatformContext* ctx,
        uint32_t pf_id,
        uint32_t oms_order_id,
        int64_t  new_price,
        int32_t  new_qty
    );
    
    int32_t (*place_cancel_order)(
        PlatformContext* ctx,
        uint32_t pf_id,
        uint32_t oms_order_id
    );
    
    int32_t (*get_position)(
        PlatformContext* ctx,
        uint32_t pf_id,
        uint32_t token,
        PositionView* out
    );
    
    int32_t (*get_open_orders)(
        PlatformContext* ctx,
        uint32_t pf_id,
        OpenOrderView* out_buf,
        int32_t max_count
    );
    
    void (*log_msg)(
        PlatformContext* ctx,
        uint32_t pf_id,
        const char* msg,
        uint32_t len
    );

    void (*send_status_update)(
        PlatformContext* ctx,
        uint32_t pf_id,
        const StrategyStatusUpdate* update
    );
    
} PlatformAPI;

/* ═══════════════════════════════════════════════════════════
 * FRONTEND COMMAND TYPES
 * ═══════════════════════════════════════════════════════════ */

typedef enum : uint8_t {
    FRONTEND_CMD_ADD = 1,
    FRONTEND_CMD_EDIT = 2,
    FRONTEND_CMD_RUN = 3,
    FRONTEND_CMD_STOP = 4,
    FRONTEND_CMD_REMOVE = 5,
    FRONTEND_CMD_QUERY = 6
} FrontendCmdType;

typedef struct {
    FrontendCmdType cmd_type;
    uint32_t        pf_id;
    uint32_t        payload_len;
    uint8_t         payload[4096];
} __attribute__((packed)) FrontendCommand;

typedef struct {
    uint32_t        pf_id;
    int32_t         status;
    uint32_t        response_len;
    uint8_t         response[4096];
} __attribute__((packed)) FrontendResponse;

/* ═══════════════════════════════════════════════════════════
 * STRATEGY CALLBACKS
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int32_t (*on_add)(
        void* handle,
        PlatformContext* ctx,
        const PlatformAPI* api,
        uint32_t pf_id,
        const uint8_t* params,
        uint32_t params_len,
        uint8_t* response_out,
        uint32_t* response_len_out
    );
    
    int32_t (*on_edit)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        const uint8_t* params,
        uint32_t params_len,
        uint8_t* response_out,
        uint32_t* response_len_out
    );
    
    int32_t (*on_run)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    );
    
    int32_t (*on_stop)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    );
    
    int32_t (*on_remove)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    );
    
    int32_t (*on_query)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    );
    
    void (*on_market_event)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        const MarketEvent* event
    );
    
    void (*on_order_update)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        const OrderUpdate* update
    );
    
} StrategyFnTable;

/* ═══════════════════════════════════════════════════════════
 * TOKEN SUBSCRIPTION
 * ═══════════════════════════════════════════════════════════ */

void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token);
void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token);

/* ═══════════════════════════════════════════════════════════
 * STRATEGY EXPORTS (must be in .so)
 * ═══════════════════════════════════════════════════════════ */

uint32_t strategy_get_type_id(void);
void* strategy_create(StrategyFnTable* tbl_out);
void strategy_destroy_all(void);

#ifdef __cplusplus
}
#endif

#endif // STRATEGY_SDK_H
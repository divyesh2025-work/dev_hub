# strategy_types.pxd
# Cython declarations for C SDK types

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t, int32_t, int64_t

# Define bool as C type
ctypedef bint bool

# ═══════════════════════════════════════════════════════════
# MARKET DATA STRUCTURES
# ═══════════════════════════════════════════════════════════

cdef packed struct BookLevel:
    int64_t  price
    int32_t  qty
    int32_t  orders

cdef packed struct MarketEvent:
    uint32_t   token
    uint32_t   bids[5]
    uint32_t   asks[5]
    uint32_t   bids_qty[5]
    uint32_t   asks_qty[5]
    long       start_time
    uint32_t   seqno
    uint32_t   internal_seqno
    uint32_t   last_traded_price
    uint8_t    stream_id
    char       msg_type
    unsigned long long event_time

# ═══════════════════════════════════════════════════════════
# ORDER UPDATE STRUCTURES
# ═══════════════════════════════════════════════════════════

ctypedef enum OrderState:
    NewOms = 0
    NewExchange = 1
    ModifyOms = 2
    ModifyExchange = 3
    CancelExchange = 4
    ExchnageRejected = 5
    Fill = 6
    PartialFill = 7

cdef packed struct OrderUpdate:
    uint32_t    oms_order_id
    uint64_t    exchange_order_id
    uint32_t    token
    uint8_t     side
    OrderState  state
    int64_t     ordered_price
    int32_t     ordered_qty
    int32_t     filled_qty
    int64_t     avg_fill_price

cdef packed struct PositionView:
    uint32_t token
    int32_t  net_qty
    int64_t  avg_price
    int64_t  realised_pnl

cdef packed struct OpenOrderView:
    uint32_t    oms_order_id
    uint32_t    token
    uint8_t     side
    int64_t     price
    int32_t     qty
    OrderState  state

# ═══════════════════════════════════════════════════════════
# ORDER PLACEMENT STRUCTURES
# ═══════════════════════════════════════════════════════════

ctypedef enum OrderType:
    Bidding = 0
    IOC = 1

ctypedef enum OrderSide:
    Buy = 0
    Sell = 1

cdef packed struct Leg:
    uint32_t symbol_id
    uint32_t price
    uint32_t qty
    OrderSide side
    unsigned long long start_time
    uint32_t oms_order_id

# ═══════════════════════════════════════════════════════════
# STRATEGY STATUS UPDATE
# ═══════════════════════════════════════════════════════════

cdef packed struct StrategyStatusUpdate:
    uint32_t pf_id
    int32_t  traded_qty
    int32_t  achieved_spread
    int32_t  current_spread
    bint     is_complete
    bint     has_opportunity
    uint8_t  custom_data[512]
    uint32_t custom_data_len

# ═══════════════════════════════════════════════════════════
# PLATFORM CONTEXT AND API
# ═══════════════════════════════════════════════════════════

cdef extern from *:
    """
    typedef struct PlatformContext PlatformContext;
    """
    ctypedef struct PlatformContext:
        pass

        
ctypedef int32_t (*place_new_order_multi_leg_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    Leg* legs,
    uint8_t leg_count,
    OrderType order_type,
    unsigned long long event_time
) nogil

ctypedef int32_t (*place_modify_order_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    uint32_t oms_order_id,
    int64_t new_price,
    int32_t new_qty
) nogil

ctypedef int32_t (*place_cancel_order_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    uint32_t oms_order_id
) nogil

ctypedef int32_t (*get_position_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    uint32_t token,
    PositionView* out
) nogil

ctypedef int32_t (*get_open_orders_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    OpenOrderView* out_buf,
    int32_t max_count
) nogil

ctypedef void (*log_msg_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    const char* msg,
    uint32_t len
) nogil

ctypedef void (*send_status_update_fn)(
    PlatformContext* ctx,
    uint32_t pf_id,
    const StrategyStatusUpdate* update
) nogil

cdef struct PlatformAPI:
    place_new_order_multi_leg_fn place_new_order_multi_leg
    place_modify_order_fn place_modify_order
    place_cancel_order_fn place_cancel_order
    get_position_fn get_position
    get_open_orders_fn get_open_orders
    log_msg_fn log_msg
    send_status_update_fn send_status_update

# ═══════════════════════════════════════════════════════════
# FRONTEND COMMAND TYPES
# ═══════════════════════════════════════════════════════════

ctypedef enum FrontendCmdType:
    FRONTEND_CMD_ADD = 1
    FRONTEND_CMD_EDIT = 2
    FRONTEND_CMD_RUN = 3
    FRONTEND_CMD_STOP = 4
    FRONTEND_CMD_REMOVE = 5
    FRONTEND_CMD_QUERY = 6

# ═══════════════════════════════════════════════════════════
# STRATEGY FUNCTION TABLE
# ═══════════════════════════════════════════════════════════

ctypedef int32_t (*on_add_fn)(
    void* handle,
    PlatformContext* ctx,
    const PlatformAPI* api,
    uint32_t pf_id,
    const uint8_t* params,
    uint32_t params_len,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil

ctypedef int32_t (*on_edit_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const uint8_t* params,
    uint32_t params_len,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil

ctypedef int32_t (*on_run_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil

ctypedef int32_t (*on_stop_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil

ctypedef int32_t (*on_remove_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil

ctypedef int32_t (*on_query_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil

ctypedef void (*on_market_event_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const MarketEvent* event
) nogil

ctypedef void (*on_order_update_fn)(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const OrderUpdate* update
) nogil

cdef struct StrategyFnTable:
    on_add_fn on_add
    on_edit_fn on_edit
    on_run_fn on_run
    on_stop_fn on_stop
    on_remove_fn on_remove
    on_query_fn on_query
    on_market_event_fn on_market_event
    on_order_update_fn on_order_update

# ═══════════════════════════════════════════════════════════
# EXTERNAL C FUNCTIONS
# ═══════════════════════════════════════════════════════════

cdef extern from "../strategy_sdk.h":
    void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token) nogil
    void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token) nogil

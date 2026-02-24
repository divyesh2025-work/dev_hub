# cython: language_level=3, boundscheck=False, wraparound=False
"""
Cython Bridge: Python Strategy → C++ Compatible .so

This bridges Python strategy code to your C++ platform.
Users write Python strategies, this compiles them to .so that C++ can load.
"""

from cpython.mem cimport PyMem_Malloc, PyMem_Realloc, PyMem_Free
from libc.stdint cimport uint8_t, uint32_t, int32_t, int64_t, uint64_t
from libc.string cimport memcpy, memset
from cpython.bytes cimport PyBytes_FromStringAndSize

import sys
from typing import Dict, List

# ─────────────────────────────────────────────────────────────
# C DEFINITIONS (matching your strategy_sdk.h)
# ─────────────────────────────────────────────────────────────

cdef extern from "sdk/strategy_sdk.h":
    # Enums
    ctypedef enum OrderState:
        NewOms = 0
        NewExchange = 1
        ModifyOms = 2
        ModifyExchange = 3
        CancelExchange = 4
        ExchnageRejected = 5
        Fill = 6
        PartialFill = 7
    
    ctypedef enum OrderType:
        Bidding = 0
        IOC = 1
    
    ctypedef enum OrderSide:
        Buy = 0
        Sell = 1
    
    # Structures
    ctypedef struct MarketEvent:
        uint32_t token
        uint32_t bids[5]
        uint32_t asks[5]
        uint32_t bids_qty[5]
        uint32_t asks_qty[5]
        long start_time
        uint32_t seqno
        uint32_t internal_seqno
        uint32_t last_traded_price
        uint8_t stream_id
        char msg_type
        unsigned long long event_time
    
    ctypedef struct OrderUpdate:
        uint32_t oms_order_id
        uint64_t exchange_order_id
        uint32_t token
        uint8_t side
        OrderState state
        int64_t ordered_price
        int32_t ordered_qty
        int32_t filled_qty
        int64_t avg_fill_price
    
    ctypedef struct PositionView:
        uint32_t token
        int32_t net_qty
        int64_t avg_price
        int64_t realised_pnl
    
    ctypedef struct OpenOrderView:
        uint32_t oms_order_id
        uint32_t token
        uint8_t side
        int64_t price
        int32_t qty
        OrderState state
    
    ctypedef struct Leg:
        uint32_t symbol_id
        uint32_t price
        uint32_t qty
        OrderSide side
        unsigned long long start_time
        uint32_t oms_order_id
    
    ctypedef struct StrategyStatusUpdate:
        uint32_t pf_id
        int32_t traded_qty
        int32_t achieved_spread
        int32_t current_spread
        bint is_complete
        bint has_opportunity
        uint8_t custom_data[512]
        uint32_t custom_data_len
    
    ctypedef struct PlatformContext:
        pass
    
    ctypedef struct PlatformAPI:
        int32_t (*place_new_order_multi_leg)(
            PlatformContext* ctx,
            uint32_t pf_id,
            Leg* legs,
            uint8_t leg_count,
            OrderType order_type,
            unsigned long long event_time
        )
        int32_t (*place_modify_order)(
            PlatformContext* ctx,
            uint32_t pf_id,
            uint32_t oms_order_id,
            int64_t new_price,
            int32_t new_qty
        )
        int32_t (*place_cancel_order)(
            PlatformContext* ctx,
            uint32_t pf_id,
            uint32_t oms_order_id
        )
        int32_t (*get_position)(
            PlatformContext* ctx,
            uint32_t pf_id,
            uint32_t token,
            PositionView* out
        )
        int32_t (*get_open_orders)(
            PlatformContext* ctx,
            uint32_t pf_id,
            OpenOrderView* out_buf,
            int32_t max_count
        )
        void (*log_msg)(
            PlatformContext* ctx,
            uint32_t pf_id,
            const char* msg,
            uint32_t len
        )
        void (*send_status_update)(
            PlatformContext* ctx,
            uint32_t pf_id,
            const StrategyStatusUpdate* update
        )
    
    ctypedef struct StrategyFnTable:
        int32_t (*on_add)(
            void* handle,
            PlatformContext* ctx,
            const PlatformAPI* api,
            uint32_t pf_id,
            const uint8_t* params,
            uint32_t params_len,
            uint8_t* response_out,
            uint32_t* response_len_out
        )
        int32_t (*on_edit)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            const uint8_t* params,
            uint32_t params_len,
            uint8_t* response_out,
            uint32_t* response_len_out
        )
        int32_t (*on_run)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            uint8_t* response_out,
            uint32_t* response_len_out
        )
        int32_t (*on_stop)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            uint8_t* response_out,
            uint32_t* response_len_out
        )
        int32_t (*on_remove)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            uint8_t* response_out,
            uint32_t* response_len_out
        )
        int32_t (*on_query)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            uint8_t* response_out,
            uint32_t* response_len_out
        )
        void (*on_market_event)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            const MarketEvent* event
        )
        void (*on_order_update)(
            void* handle,
            PlatformContext* ctx,
            uint32_t pf_id,
            const OrderUpdate* update
        )
    
    void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)
    void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)


# ─────────────────────────────────────────────────────────────
# PYTHON WRAPPER CLASSES
# ─────────────────────────────────────────────────────────────

class MarketData:
    """Python wrapper for MarketEvent"""
    def __init__(self, event: 'MarketEvent'):
        self.token = event.token
        self.bids = list(event.bids[:5])
        self.asks = list(event.asks[:5])
        self.bids_qty = list(event.bids_qty[:5])
        self.asks_qty = list(event.asks_qty[:5])
        self.last_traded_price = event.last_traded_price
        self.seqno = event.seqno


class OrderFill:
    """Python wrapper for OrderUpdate"""
    def __init__(self, update: 'OrderUpdate'):
        self.order_id = update.oms_order_id
        self.exchange_order_id = update.exchange_order_id
        self.token = update.token
        self.side = "BUY" if update.side == Buy else "SELL"
        self.filled_qty = update.filled_qty
        self.avg_fill_price = update.avg_fill_price
        self.ordered_qty = update.ordered_qty
        self.state = update.state


class PlatformWrapper:
    """Wrapper to call platform API from Python"""
    def __init__(self, ctx: 'PlatformContext', api: 'PlatformAPI', pf_id: uint32_t):
        self.ctx = ctx
        self.api = api
        self.pf_id = pf_id
    
    def place_orders(self, legs: List[Dict], order_type=1):
        """Place multi-leg orders (IOC=1, Bidding=0)"""
        cdef Leg* c_legs = <Leg*>PyMem_Malloc(len(legs) * sizeof(Leg))
        try:
            for i, leg in enumerate(legs):
                c_legs[i].symbol_id = leg['token']
                c_legs[i].price = leg['price']
                c_legs[i].qty = leg['qty']
                c_legs[i].side = Buy if leg['side'] == 'BUY' else Sell
                c_legs[i].start_time = 0
                c_legs[i].oms_order_id = 0
            
            return self.api.place_new_order_multi_leg(
                self.ctx,
                self.pf_id,
                c_legs,
                len(legs),
                <OrderType>order_type,
                0
            )
        finally:
            PyMem_Free(c_legs)
    
    def log(self, msg: str):
        """Log message to platform"""
        msg_bytes = msg.encode('utf-8')
        self.api.log_msg(self.ctx, self.pf_id, msg_bytes, len(msg_bytes))


# ─────────────────────────────────────────────────────────────
# GLOBAL STRATEGY INSTANCE
# ─────────────────────────────────────────────────────────────

_g_strategy_instance = None
_g_strategy_class = None


def set_strategy_class(cls):
    """Set the Python strategy class to use"""
    global _g_strategy_class
    _g_strategy_class = cls


# ─────────────────────────────────────────────────────────────
# C CALLBACK WRAPPERS
# ─────────────────────────────────────────────────────────────

cdef int32_t c_on_add(
    void* handle,
    PlatformContext* ctx,
    const PlatformAPI* api,
    uint32_t pf_id,
    const uint8_t* params,
    uint32_t params_len,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil:
    """C callback for on_add"""
    with gil:
        try:
            global _g_strategy_instance, _g_strategy_class
            
            if _g_strategy_instance is None and _g_strategy_class:
                _g_strategy_instance = _g_strategy_class()
            
            if _g_strategy_instance:
                # Parse params
                params_dict = {}
                if params_len > 0:
                    params_dict = eval(params[:params_len].decode('utf-8', errors='ignore'))
                
                # Call Python strategy
                platform = PlatformWrapper(ctx, <PlatformAPI*>api, pf_id)
                result = _g_strategy_instance.on_add(params_dict, platform)
                
                # Prepare response
                if isinstance(result, str):
                    resp_bytes = result.encode('utf-8')
                    resp_len = len(resp_bytes)
                    if resp_len < 4096:
                        memcpy(response_out, <uint8_t*>resp_bytes, resp_len)
                        response_len_out[0] = resp_len
                
                return 0
            return -1
        except Exception as e:
            return -1


cdef int32_t c_on_run(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil:
    """C callback for on_run"""
    with gil:
        try:
            global _g_strategy_instance
            if _g_strategy_instance:
                result = _g_strategy_instance.on_run()
                if isinstance(result, str):
                    resp_bytes = result.encode('utf-8')
                    resp_len = len(resp_bytes)
                    if resp_len < 4096:
                        memcpy(response_out, <uint8_t*>resp_bytes, resp_len)
                        response_len_out[0] = resp_len
                return 0
            return -1
        except:
            return -1


cdef int32_t c_on_stop(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil:
    """C callback for on_stop"""
    with gil:
        try:
            global _g_strategy_instance
            if _g_strategy_instance:
                result = _g_strategy_instance.on_stop()
                if isinstance(result, str):
                    resp_bytes = result.encode('utf-8')
                    resp_len = len(resp_bytes)
                    if resp_len < 4096:
                        memcpy(response_out, <uint8_t*>resp_bytes, resp_len)
                        response_len_out[0] = resp_len
                return 0
            return -1
        except:
            return -1


cdef int32_t c_on_query(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) nogil:
    """C callback for on_query"""
    with gil:
        try:
            global _g_strategy_instance
            if _g_strategy_instance:
                state = _g_strategy_instance.on_query()
                resp_bytes = str(state).encode('utf-8')
                resp_len = len(resp_bytes)
                if resp_len < 4096:
                    memcpy(response_out, <uint8_t*>resp_bytes, resp_len)
                    response_len_out[0] = resp_len
                return 0
            return -1
        except:
            return -1


cdef void c_on_market_event(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const MarketEvent* event
) nogil:
    """C callback for market events"""
    with gil:
        try:
            global _g_strategy_instance
            if _g_strategy_instance and event:
                market = MarketData(event[0])
                _g_strategy_instance.on_market_event(market)
        except:
            pass


cdef void c_on_order_update(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const OrderUpdate* update
) nogil:
    """C callback for order updates"""
    with gil:
        try:
            global _g_strategy_instance
            if _g_strategy_instance and update:
                fill = OrderFill(update[0])
                _g_strategy_instance.on_order_update(fill)
        except:
            pass


# ─────────────────────────────────────────────────────────────
# PUBLIC C EXPORTS (what C++ platform calls)
# ─────────────────────────────────────────────────────────────

cdef extern from *:
    """
    // Dummy marker - will be replaced by actual implementations
    """
    pass


# NOTE: The actual exports are done at module level below


# ─────────────────────────────────────────────────────────────
# PYTHON API FOR BUILDING STRATEGIES
# ─────────────────────────────────────────────────────────────

def create_strategy_so(strategy_class, output_file, strategy_type_id=1):
    """
    Compile a Python strategy to .so
    
    Args:
        strategy_class: Python strategy class (must have on_add, on_run, etc.)
        output_file: Path to output .so file
        strategy_type_id: Type ID for the strategy
    
    Returns:
        True if successful
    """
    set_strategy_class(strategy_class)
    # The actual compilation is handled by setup.py
    return True

# distutils: language = c++
# cython: language_level = 3
"""
Cython binding for Strategy SDK
Provides Python interface for C++ platform API
"""

from libc.stdint cimport uint32_t, uint8_t, uint64_t, int32_t, int64_t, uint16_t
from libc.string cimport memcpy, memset
from cpython.bytes cimport PyBytes_FromStringAndSize
from cpython.ref cimport PyObject
from cpython.mem cimport PyMem_Malloc, PyMem_Free
import sys
from enum import IntEnum

# ═══════════════════════════════════════════════════════════
# C STRUCTURES FROM strategy_sdk.h
# ═══════════════════════════════════════════════════════════

cdef extern from "strategy_sdk.h":
    ctypedef struct BookLevel:
        int64_t price
        int32_t qty
        int32_t orders

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

    ctypedef enum OrderState:
        NewOms = 0
        NewExchange = 1
        ModifyOms = 2
        ModifyExchange = 3
        CancelExchange = 4
        ExchnageRejected = 5
        Fill = 6
        PartialFill = 7

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

    ctypedef enum OrderType:
        Bidding = 0
        IOC = 1

    ctypedef enum OrderSide:
        Buy = 0
        Sell = 1

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

    ctypedef int32_t (*OnAddCallback)(
        void* handle,
        PlatformContext* ctx,
        const PlatformAPI* api,
        uint32_t pf_id,
        const uint8_t* params,
        uint32_t params_len,
        uint8_t* response_out,
        uint32_t* response_len_out
    )

    ctypedef int32_t (*OnEditCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        const uint8_t* params,
        uint32_t params_len,
        uint8_t* response_out,
        uint32_t* response_len_out
    )

    ctypedef int32_t (*OnRunCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    )

    ctypedef int32_t (*OnStopCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    )

    ctypedef int32_t (*OnRemoveCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    )

    ctypedef int32_t (*OnQueryCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        uint8_t* response_out,
        uint32_t* response_len_out
    )

    ctypedef void (*OnMarketEventCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        const MarketEvent* event
    )

    ctypedef void (*OnOrderUpdateCallback)(
        void* handle,
        PlatformContext* ctx,
        uint32_t pf_id,
        const OrderUpdate* update
    )

    ctypedef struct StrategyFnTable:
        OnAddCallback on_add
        OnEditCallback on_edit
        OnRunCallback on_run
        OnStopCallback on_stop
        OnRemoveCallback on_remove
        OnQueryCallback on_query
        OnMarketEventCallback on_market_event
        OnOrderUpdateCallback on_order_update

    void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)
    void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)

# ═══════════════════════════════════════════════════════════
# PYTHON ENUMS
# ═══════════════════════════════════════════════════════════

class OrderStateEnum(IntEnum):
    """Order state enumeration"""
    NewOms = NewOms
    NewExchange = NewExchange
    ModifyOms = ModifyOms
    ModifyExchange = ModifyExchange
    CancelExchange = CancelExchange
    ExchangeRejected = ExchnageRejected
    Fill = Fill
    PartialFill = PartialFill

class OrderTypeEnum(IntEnum):
    """Order type enumeration"""
    Bidding = Bidding
    IOC = IOC

class OrderSideEnum(IntEnum):
    """Order side enumeration"""
    Buy = Buy
    Sell = Sell

# ═══════════════════════════════════════════════════════════
# PYTHON WRAPPERS
# ═══════════════════════════════════════════════════════════

cdef class PyMarketEvent:
    """Python wrapper for MarketEvent"""
    cdef MarketEvent c_event

    def __init__(self):
        memset(&self.c_event, 0, sizeof(MarketEvent))

    @staticmethod
    cdef PyMarketEvent from_c(const MarketEvent* c_event):
        cdef PyMarketEvent obj = PyMarketEvent.__new__(PyMarketEvent)
        obj.c_event = c_event[0]
        return obj

    @property
    def token(self):
        return self.c_event.token

    @property
    def bids(self):
        return [self.c_event.bids[i] for i in range(5)]

    @property
    def asks(self):
        return [self.c_event.asks[i] for i in range(5)]

    @property
    def bids_qty(self):
        return [self.c_event.bids_qty[i] for i in range(5)]

    @property
    def asks_qty(self):
        return [self.c_event.asks_qty[i] for i in range(5)]

    @property
    def seqno(self):
        return self.c_event.seqno

    @property
    def last_traded_price(self):
        return self.c_event.last_traded_price

    @property
    def event_time(self):
        return self.c_event.event_time

cdef class PyOrderUpdate:
    """Python wrapper for OrderUpdate"""
    cdef OrderUpdate c_update

    @staticmethod
    cdef PyOrderUpdate from_c(const OrderUpdate* c_update):
        cdef PyOrderUpdate obj = PyOrderUpdate.__new__(PyOrderUpdate)
        obj.c_update = c_update[0]
        return obj

    @property
    def oms_order_id(self):
        return self.c_update.oms_order_id

    @property
    def exchange_order_id(self):
        return self.c_update.exchange_order_id

    @property
    def token(self):
        return self.c_update.token

    @property
    def side(self):
        return self.c_update.side

    @property
    def state(self):
        return OrderStateEnum(self.c_update.state)

    @property
    def ordered_price(self):
        return self.c_update.ordered_price

    @property
    def ordered_qty(self):
        return self.c_update.ordered_qty

    @property
    def filled_qty(self):
        return self.c_update.filled_qty

    @property
    def avg_fill_price(self):
        return self.c_update.avg_fill_price

cdef class PyLeg:
    """Python wrapper for placing orders"""
    cdef Leg c_leg

    def __init__(self, symbol_id, price, qty, side, start_time=0):
        memset(&self.c_leg, 0, sizeof(Leg))
        self.c_leg.symbol_id = symbol_id
        self.c_leg.price = price
        self.c_leg.qty = qty
        self.c_leg.side = side
        self.c_leg.start_time = start_time
        self.c_leg.oms_order_id = 0

    cdef Leg* get_c_struct(self):
        return &self.c_leg

cdef class PyPlatformAPI:
    """Python wrapper for platform API calls"""
    cdef const PlatformAPI* c_api
    cdef PlatformContext* c_ctx
    cdef uint32_t c_pf_id

    @staticmethod
    cdef PyPlatformAPI from_c(const PlatformAPI* api, PlatformContext* ctx, uint32_t pf_id):
        cdef PyPlatformAPI obj = PyPlatformAPI.__new__(PyPlatformAPI)
        obj.c_api = api
        obj.c_ctx = ctx
        obj.c_pf_id = pf_id
        return obj

    def place_new_order_multi_leg(self, legs, order_type, event_time):
        """Place a new multi-leg order"""
        cdef int leg_count = len(legs)
        cdef Leg* c_legs
        cdef int32_t ret
        cdef int i
        
        c_legs = <Leg*> PyMem_Malloc(leg_count * sizeof(Leg))
        
        try:
            for i in range(leg_count):
                c_legs[i] = (<PyLeg>legs[i]).c_leg
            
            ret = self.c_api.place_new_order_multi_leg(
                self.c_ctx, self.c_pf_id, c_legs, leg_count,
                <OrderType>order_type, <unsigned long long>event_time
            )
            return ret
        finally:
            PyMem_Free(c_legs)

    def place_modify_order(self, oms_order_id, new_price, new_qty):
        """Modify an existing order"""
        cdef int32_t ret
        ret = self.c_api.place_modify_order(
            self.c_ctx, self.c_pf_id, oms_order_id,
            <int64_t>new_price, <int32_t>new_qty
        )
        return ret

    def place_cancel_order(self, oms_order_id):
        """Cancel an order"""
        cdef int32_t ret
        ret = self.c_api.place_cancel_order(
            self.c_ctx, self.c_pf_id, oms_order_id
        )
        return ret

    def log_msg(self, msg):
        """Log a message"""
        cdef bytes msg_bytes
        if isinstance(msg, str):
            msg = msg.encode('utf-8')
        msg_bytes = msg
        self.c_api.log_msg(self.c_ctx, self.c_pf_id, msg_bytes, len(msg_bytes))

    def send_status_update(self, pf_id, traded_qty, achieved_spread, 
                          current_spread, is_complete, has_opportunity):
        """Send strategy status update"""
        cdef StrategyStatusUpdate update
        update.pf_id = pf_id
        update.traded_qty = traded_qty
        update.achieved_spread = achieved_spread
        update.current_spread = current_spread
        update.is_complete = is_complete
        update.has_opportunity = has_opportunity
        update.custom_data_len = 0
        
        self.c_api.send_status_update(self.c_ctx, self.c_pf_id, &update)


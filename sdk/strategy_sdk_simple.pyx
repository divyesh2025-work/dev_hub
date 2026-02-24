# distutils: language = c++
# cython: language_level = 3
"""
Simplified Cython binding for Strategy SDK
"""

from libc.stdint cimport uint32_t, uint8_t, uint64_t, int32_t, int64_t
from libc.string cimport memcpy, memset
from cpython.mem cimport PyMem_Malloc, PyMem_Free

# C structures from strategy_sdk.h
cdef extern from "strategy_sdk.h":
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
        uint8_t state
        int64_t ordered_price
        int32_t ordered_qty
        int32_t filled_qty
        int64_t avg_fill_price

    ctypedef struct Leg:
        uint32_t symbol_id
        uint32_t price
        uint32_t qty
        uint8_t side
        unsigned long long start_time
        uint32_t oms_order_id

    ctypedef struct PlatformContext:
        pass

    ctypedef struct PlatformAPI:
        int32_t (*place_new_order_multi_leg)(
            PlatformContext*,
            uint32_t,
            Leg*,
            uint8_t,
            uint8_t,
            unsigned long long
        )
        void (*log_msg)(
            PlatformContext*,
            uint32_t,
            const char*,
            uint32_t
        )
        void (*send_status_update)(
            PlatformContext*,
            uint32_t,
            void*
        )

# Python wrappers
cdef class PyMarketEvent:
    cdef MarketEvent c_event

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
    def event_time(self):
        return self.c_event.event_time


cdef class PyOrderUpdate:
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
    def token(self):
        return self.c_update.token

    @property
    def filled_qty(self):
        return self.c_update.filled_qty


cdef class PyLeg:
    cdef Leg c_leg

    def __init__(self, symbol_id, price, qty, side, start_time=0):
        self.c_leg.symbol_id = symbol_id
        self.c_leg.price = price
        self.c_leg.qty = qty
        self.c_leg.side = side
        self.c_leg.start_time = start_time

    cdef Leg* get_c_struct(self):
        return &self.c_leg

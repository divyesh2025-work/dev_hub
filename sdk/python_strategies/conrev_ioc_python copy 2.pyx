# python_strategies/conrev_ioc_standalone.pyx
# Standalone Cython strategy - compiles to single .so

from libc.stdint cimport uint8_t, uint32_t, int32_t, int64_t, uint64_t
from libc.string cimport memcpy, strlen, memset
from cpython.ref cimport PyObject, Py_INCREF
import json

# ═══════════════════════════════════════════════════════════
# C TYPE DECLARATIONS (inline from strategy_sdk.h)
# ═══════════════════════════════════════════════════════════

cdef extern from "../strategy_sdk.h":
    ctypedef struct PlatformContext:
        pass
    
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
        uint8_t state  # OrderState enum
        int64_t ordered_price
        int32_t ordered_qty
        int32_t filled_qty
        int64_t avg_fill_price
    
    ctypedef struct Leg:
        uint32_t symbol_id
        uint32_t price
        uint32_t qty
        uint8_t side  # OrderSide enum
        unsigned long long start_time
        uint32_t oms_order_id
    
    ctypedef struct PlatformAPI:
        int32_t (*place_new_order_multi_leg)(
            PlatformContext* ctx, uint32_t pf_id, Leg* legs,
            uint8_t leg_count, uint8_t order_type, unsigned long long event_time
        )
        void (*log_msg)(PlatformContext* ctx, uint32_t pf_id, const char* msg, uint32_t len)
        int32_t (*place_cancel_order)(PlatformContext* ctx, uint32_t pf_id, uint32_t oms_order_id)
    
    ctypedef struct StrategyFnTable:
        pass  # Will fill with function pointers
    
    void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)
    void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)

# Constants
DEF CONREV_IOC_PYTHON_TYPE_ID = 101
DEF BUY = 0
DEF SELL = 1
DEF IOC = 1
DEF FILL = 6
DEF PARTIAL_FILL = 7
DEF CANCEL_EXCHANGE = 4
DEF REJECT = 5

# ═══════════════════════════════════════════════════════════
# STRATEGY STATE
# ═══════════════════════════════════════════════════════════

cdef class ConRevIOCStrategy:
    # Parameters
    cdef uint32_t fut_token, call_token, put_token
    cdef int32_t strike_price, max_lots, spread_threshold
    cdef bint is_conversion
    
    # Runtime state
    cdef bint active
    cdef int32_t traded_qty, achieved_spread
    cdef bint legs_sent
    
    # Market cache
    cdef uint32_t fut_bid, fut_ask, call_bid, call_ask, put_bid, put_ask
    cdef bint fut_valid, call_valid, put_valid
    
    # Order tracking
    cdef uint32_t fut_oms_id, call_oms_id, put_oms_id
    cdef uint32_t parent_oms_ids[64]
    cdef int parent_oms_count
    
    # Platform API
    cdef PlatformContext* ctx
    cdef const PlatformAPI* api
    cdef uint32_t pf_id
    
    def __cinit__(self):
        self.active = False
        self.traded_qty = 0
        self.legs_sent = False
        self.fut_valid = self.call_valid = self.put_valid = False
        self.parent_oms_count = 0
    
    cdef void init_api(self, PlatformContext* ctx, const PlatformAPI* api, uint32_t pf_id) noexcept nogil:
        self.ctx = ctx
        self.api = api
        self.pf_id = pf_id
    
    cdef inline bint compute_opportunity(self, int32_t* spread_out) noexcept nogil:
        if not (self.fut_valid and self.call_valid and self.put_valid):
            return False
        cdef int64_t spread
        if self.is_conversion:
            spread = <int64_t>self.strike_price - self.fut_ask + self.call_bid - self.put_ask
        else:
            spread = -<int64_t>self.strike_price + self.fut_bid - self.call_ask + self.put_bid
        spread_out[0] = <int32_t>spread
        return spread >= self.spread_threshold
    
    cdef inline bint is_known_parent(self, uint32_t oms_id) noexcept nogil:
        cdef int i
        for i in range(self.parent_oms_count):
            if self.parent_oms_ids[i] == oms_id:
                return True
        return False

# Global instance
cdef ConRevIOCStrategy g_strategy = None

# ═══════════════════════════════════════════════════════════
# C CALLBACKS
# ═══════════════════════════════════════════════════════════

cdef int32_t cb_on_add(void* handle, PlatformContext* ctx, const PlatformAPI* api,
                       uint32_t pf_id, const uint8_t* params, uint32_t params_len,
                       uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    with gil:
        try:
            s = <ConRevIOCStrategy>(<PyObject*>handle)
            s.init_api(ctx, api, pf_id)
            
            # Parse JSON params
            params_bytes = params[:params_len]
            p = json.loads(params_bytes.decode('utf-8'))
            
            s.fut_token = p['fut_token']
            s.call_token = p['call_token']
            s.put_token = p['put_token']
            s.strike_price = p['strike_price']
            s.max_lots = p['max_lots']
            s.spread_threshold = p['spread_threshold']
            s.is_conversion = p['is_conversion']
            
            resp = b"Portfolio added"
            memcpy(response_out, <char*>resp, len(resp))
            response_len_out[0] = len(resp)
            return 0
        except:
            resp = b"Error"
            memcpy(response_out, <char*>resp, 5)
            response_len_out[0] = 5
            return -1

cdef int32_t cb_on_run(void* handle, PlatformContext* ctx, uint32_t pf_id,
                       uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    with gil:
        try:
            s = <ConRevIOCStrategy>(<PyObject*>handle)
            platform_subscribe_token(ctx, pf_id, s.fut_token)
            platform_subscribe_token(ctx, pf_id, s.call_token)
            platform_subscribe_token(ctx, pf_id, s.put_token)
            s.active = True
            s.legs_sent = False
            
            resp = b"Strategy running"
            memcpy(response_out, <char*>resp, len(resp))
            response_len_out[0] = len(resp)
            return 0
        except:
            return -1

cdef void cb_on_market_event(void* handle, PlatformContext* ctx, uint32_t pf_id,
                              const MarketEvent* ev) noexcept nogil:
    cdef ConRevIOCStrategy s = <ConRevIOCStrategy>(<PyObject*>handle)
    if not s.active:
        return
    
    # Cache market data
    if ev.token == s.fut_token:
        s.fut_bid = ev.bids[0]
        s.fut_ask = ev.asks[0]
        s.fut_valid = True
    elif ev.token == s.call_token:
        s.call_bid = ev.bids[0]
        s.call_ask = ev.asks[0]
        s.call_valid = True
    elif ev.token == s.put_token:
        s.put_bid = ev.bids[0]
        s.put_ask = ev.asks[0]
        s.put_valid = True
    
    if s.legs_sent or s.traded_qty >= s.max_lots:
        return
    
    cdef int32_t spread
    if not s.compute_opportunity(&spread):
        return
    
    # Place orders
    cdef uint32_t qty = s.max_lots - s.traded_qty
    cdef Leg legs[3]
    
    if s.is_conversion:
        legs[0] = Leg(s.fut_token, s.fut_ask, qty, BUY, 0, 0)
        legs[1] = Leg(s.call_token, s.call_bid, qty, SELL, 0, 0)
        legs[2] = Leg(s.put_token, s.put_ask, qty, BUY, 0, 0)
    else:
        legs[0] = Leg(s.fut_token, s.fut_bid, qty, SELL, 0, 0)
        legs[1] = Leg(s.call_token, s.call_ask, qty, BUY, 0, 0)
        legs[2] = Leg(s.put_token, s.put_bid, qty, SELL, 0, 0)
    
    cdef int32_t parent = s.api.place_new_order_multi_leg(ctx, pf_id, legs, 3, IOC, ev.event_time)
    if parent > 0:
        if s.parent_oms_count < 64:
            s.parent_oms_ids[s.parent_oms_count] = parent
            s.parent_oms_count += 1
        s.legs_sent = True
        s.achieved_spread = spread
        s.fut_oms_id = legs[0].oms_order_id
        s.call_oms_id = legs[1].oms_order_id
        s.put_oms_id = legs[2].oms_order_id

cdef void cb_on_order_update(void* handle, PlatformContext* ctx, uint32_t pf_id,
                              const OrderUpdate* upd) noexcept nogil:
    cdef ConRevIOCStrategy s = <ConRevIOCStrategy>(<PyObject*>handle)
    if not s.is_known_parent(upd.oms_order_id):
        return
    
    if upd.state == FILL:
        if upd.oms_order_id == s.fut_oms_id:
            s.traded_qty += upd.filled_qty
        if upd.oms_order_id == s.fut_oms_id:
            s.fut_oms_id = 0
        elif upd.oms_order_id == s.call_oms_id:
            s.call_oms_id = 0
        elif upd.oms_order_id == s.put_oms_id:
            s.put_oms_id = 0
        if s.traded_qty >= s.max_lots:
            s.active = False

# Other callbacks (minimal implementations)
cdef int32_t cb_on_edit(...) noexcept nogil: return 0
cdef int32_t cb_on_stop(...) noexcept nogil: return 0
cdef int32_t cb_on_remove(...) noexcept nogil: return 0
cdef int32_t cb_on_query(...) noexcept nogil: return 0

# ═══════════════════════════════════════════════════════════
# EXPORTS (extern "C")
# ═══════════════════════════════════════════════════════════

cdef extern from *:
    """
    extern "C" {
        uint32_t strategy_get_type_id(void) { return 101; }
        void* strategy_create(StrategyFnTable* tbl);
        void strategy_destroy_all(void);
    }
    """

cdef public void* strategy_create(void* tbl_ptr) noexcept with gil:
    global g_strategy
    try:
        g_strategy = ConRevIOCStrategy()
        Py_INCREF(g_strategy)
        
        # Cast and fill function table
        cdef StrategyFnTable* tbl = <StrategyFnTable*>tbl_ptr
        # ... fill tbl callbacks ...
        
        return <void*>g_strategy
    except:
        return NULL

cdef public void strategy_destroy_all() noexcept with gil:
    global g_strategy
    g_strategy = None
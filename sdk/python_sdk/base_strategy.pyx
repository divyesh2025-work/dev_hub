# base_strategy.pyx
# Base class for Python strategies

from strategy_types cimport (
    PlatformContext, PlatformAPI, MarketEvent, OrderUpdate,
    OrderState, Leg, OrderType, OrderSide, StrategyStatusUpdate,
    PositionView, OpenOrderView, StrategyFnTable,
    uint8_t, uint32_t, int32_t, int64_t, uint64_t,
    platform_subscribe_token, platform_unsubscribe_token
)
from platform_api cimport PlatformAPIWrapper
from libc.string cimport memcpy, strlen, memset
from libc.stdint cimport uint8_t, uint32_t, int32_t
from cpython.ref cimport PyObject, Py_INCREF, Py_DECREF
import json

# Global registry of strategy instances
cdef dict _strategy_registry = {}

# ═══════════════════════════════════════════════════════════
# BASE STRATEGY CLASS (Python developers inherit this)
# ═══════════════════════════════════════════════════════════

cdef class BaseStrategy:
    """
    Base class for Python trading strategies
    
    Subclass this and override methods:
    - on_add(params: dict) -> str
    - on_edit(params: dict) -> str
    - on_run() -> str
    - on_stop() -> str
    - on_remove() -> str
    - on_query() -> str
    - on_market_event(event: dict)
    - on_order_update(update: dict)
    """
    
    def __cinit__(self):
        self.api = None
        self.pf_id = 0
        self.user_state = {}
    
    cdef void _init_api(self, PlatformContext* ctx, const PlatformAPI* c_api, uint32_t pf_id):
        """Initialize API wrapper (called from C callbacks)"""
        self.api = PlatformAPIWrapper()
        self.api._init(ctx, c_api, pf_id)
        self.pf_id = pf_id
    
    # ═══════════════════════════════════════════════════════════
    # LIFECYCLE METHODS (override in subclass)
    # ═══════════════════════════════════════════════════════════
    
    cpdef str on_add(self, dict params):
        """Called when portfolio is added. Return success message or raise exception."""
        return "Portfolio added"
    
    cpdef str on_edit(self, dict params):
        """Called when parameters are edited. Return success message or raise exception."""
        return "Parameters updated"
    
    cpdef str on_run(self):
        """Called when strategy starts. Return success message or raise exception."""
        return "Strategy running"
    
    cpdef str on_stop(self):
        """Called when strategy stops. Return success message or raise exception."""
        return "Strategy stopped"
    
    cpdef str on_remove(self):
        """Called when portfolio is removed. Return success message or raise exception."""
        return "Portfolio removed"
    
    cpdef str on_query(self):
        """Called to query strategy state. Return JSON string."""
        return json.dumps({'pf_id': self.pf_id, 'state': self.user_state})
    
    # ═══════════════════════════════════════════════════════════
    # HOT PATH METHODS (override in subclass)
    # ═══════════════════════════════════════════════════════════
    
    cpdef void on_market_event(self, dict event):
        """
        Called on every market data update
        
        event keys: token, bids, asks, bids_qty, asks_qty, event_time, etc.
        """
        pass
    
    cpdef void on_order_update(self, dict update):
        """
        Called on every order update
        
        update keys: oms_order_id, token, side, state, ordered_price, 
                     ordered_qty, filled_qty, avg_fill_price
        """
        pass

# ═══════════════════════════════════════════════════════════
# C CALLBACK WRAPPERS (bridge between C and Python)
# ═══════════════════════════════════════════════════════════

cdef int32_t c_on_add(void* handle, PlatformContext* ctx,
                      const PlatformAPI* api, uint32_t pf_id,
                      const uint8_t* params, uint32_t params_len,
                      uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    """C callback -> Python on_add"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            
            # Initialize API wrapper
            strategy._init_api(ctx, api, pf_id)
            
            # Parse params as JSON
            params_bytes = params[:params_len]
            params_dict = json.loads(params_bytes.decode('utf-8'))
            
            # Call Python method
            response = strategy.on_add(params_dict)
            
            # Write response
            response_bytes = response.encode('utf-8')
            response_len_out[0] = min(len(response_bytes), 4096)
            memcpy(response_out, <char*>response_bytes, response_len_out[0])
            
            return 0
            
        except Exception as e:
            error = str(e).encode('utf-8')
            response_len_out[0] = min(len(error), 4096)
            memcpy(response_out, <char*>error, response_len_out[0])
            return -1

cdef int32_t c_on_edit(void* handle, PlatformContext* ctx, uint32_t pf_id,
                       const uint8_t* params, uint32_t params_len,
                       uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    """C callback -> Python on_edit"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            params_bytes = params[:params_len]
            params_dict = json.loads(params_bytes.decode('utf-8'))
            
            response = strategy.on_edit(params_dict)
            response_bytes = response.encode('utf-8')
            response_len_out[0] = min(len(response_bytes), 4096)
            memcpy(response_out, <char*>response_bytes, response_len_out[0])
            
            return 0
        except Exception as e:
            error = str(e).encode('utf-8')
            response_len_out[0] = min(len(error), 4096)
            memcpy(response_out, <char*>error, response_len_out[0])
            return -1

cdef int32_t c_on_run(void* handle, PlatformContext* ctx, uint32_t pf_id,
                      uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    """C callback -> Python on_run"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            response = strategy.on_run()
            response_bytes = response.encode('utf-8')
            response_len_out[0] = min(len(response_bytes), 4096)
            memcpy(response_out, <char*>response_bytes, response_len_out[0])
            return 0
        except Exception as e:
            error = str(e).encode('utf-8')
            response_len_out[0] = min(len(error), 4096)
            memcpy(response_out, <char*>error, response_len_out[0])
            return -1

cdef int32_t c_on_stop(void* handle, PlatformContext* ctx, uint32_t pf_id,
                       uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    """C callback -> Python on_stop"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            response = strategy.on_stop()
            response_bytes = response.encode('utf-8')
            response_len_out[0] = min(len(response_bytes), 4096)
            memcpy(response_out, <char*>response_bytes, response_len_out[0])
            return 0
        except Exception as e:
            error = str(e).encode('utf-8')
            response_len_out[0] = min(len(error), 4096)
            memcpy(response_out, <char*>error, response_len_out[0])
            return -1

cdef int32_t c_on_remove(void* handle, PlatformContext* ctx, uint32_t pf_id,
                         uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    """C callback -> Python on_remove"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            response = strategy.on_remove()
            response_bytes = response.encode('utf-8')
            response_len_out[0] = min(len(response_bytes), 4096)
            memcpy(response_out, <char*>response_bytes, response_len_out[0])
            return 0
        except Exception as e:
            error = str(e).encode('utf-8')
            response_len_out[0] = min(len(error), 4096)
            memcpy(response_out, <char*>error, response_len_out[0])
            return -1

cdef int32_t c_on_query(void* handle, PlatformContext* ctx, uint32_t pf_id,
                        uint8_t* response_out, uint32_t* response_len_out) noexcept nogil:
    """C callback -> Python on_query"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            response = strategy.on_query()
            response_bytes = response.encode('utf-8')
            response_len_out[0] = min(len(response_bytes), 4096)
            memcpy(response_out, <char*>response_bytes, response_len_out[0])
            return 0
        except Exception as e:
            error = str(e).encode('utf-8')
            response_len_out[0] = min(len(error), 4096)
            memcpy(response_out, <char*>error, response_len_out[0])
            return -1

cdef void c_on_market_event(void* handle, PlatformContext* ctx, uint32_t pf_id,
                            const MarketEvent* event) noexcept nogil:
    """C callback -> Python on_market_event (HOT PATH)"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            
            # Convert C struct to Python dict (minimal allocation)
            event_dict = {
                'token': event.token,
                'bids': [event.bids[i] for i in range(5)],
                'asks': [event.asks[i] for i in range(5)],
                'bids_qty': [event.bids_qty[i] for i in range(5)],
                'asks_qty': [event.asks_qty[i] for i in range(5)],
                'event_time': event.event_time,
                'last_traded_price': event.last_traded_price,
                'seqno': event.seqno
            }
            
            strategy.on_market_event(event_dict)
        except:
            pass  # Don't crash on exceptions in hot path

cdef void c_on_order_update(void* handle, PlatformContext* ctx, uint32_t pf_id,
                            const OrderUpdate* update) noexcept nogil:
    """C callback -> Python on_order_update"""
    with gil:
        try:
            strategy = <BaseStrategy>(<PyObject*>handle)
            
            update_dict = {
                'oms_order_id': update.oms_order_id,
                'exchange_order_id': update.exchange_order_id,
                'token': update.token,
                'side': update.side,
                'state': update.state,
                'ordered_price': update.ordered_price,
                'ordered_qty': update.ordered_qty,
                'filled_qty': update.filled_qty,
                'avg_fill_price': update.avg_fill_price
            }
            
            strategy.on_order_update(update_dict)
        except:
            pass  # Don't crash on exceptions

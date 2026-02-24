# conrev_ioc_python.pyx
# Conversion-Reversal IOC Strategy in Cython


from base_strategy cimport (
    BaseStrategy, c_on_add, c_on_edit, c_on_run, c_on_stop,
    c_on_remove, c_on_query, c_on_market_event, c_on_order_update
)
from strategy_types cimport *
from platform_api cimport PlatformAPIWrapper
from cpython.ref cimport Py_INCREF
from libc.stdint cimport uint32_t, int32_t, int64_t

# Strategy type ID
DEF CONREV_IOC_PYTHON_TYPE_ID = 101

# Order side constants
DEF BUY = 0
DEF SELL = 1

# Order type constants  
DEF IOC = 1

# Order state constants
DEF FILL = 6
DEF PARTIAL_FILL = 7
DEF CANCEL_EXCHANGE = 4
DEF REJECT = 5

# ═══════════════════════════════════════════════════════════
# CONREV IOC STRATEGY (Cython implementation)
# ═══════════════════════════════════════════════════════════

cdef class ConRevIOCStrategy(BaseStrategy):
    """
    Conversion-Reversal IOC Strategy
    
    Parameters (from on_add):
    - fut_token: Future token
    - call_token: Call option token
    - put_token: Put option token
    - strike_price: Strike price
    - max_lots: Maximum lots to trade
    - sol: Size of lot
    - spread_threshold: Minimum spread to trigger
    - is_conversion: True for conversion, False for reversal
    """
    
    # Strategy parameters
    cdef uint32_t fut_token
    cdef uint32_t call_token
    cdef uint32_t put_token
    cdef int32_t strike_price
    cdef int32_t max_lots
    cdef int32_t sol
    cdef int32_t spread_threshold
    cdef bint is_conversion
    
    # Runtime state
    cdef bint active
    cdef int32_t traded_qty
    cdef int32_t achieved_spread
    cdef bint legs_sent
    
    # Market data cache
    cdef uint32_t fut_bid, fut_ask
    cdef uint32_t call_bid, call_ask
    cdef uint32_t put_bid, put_ask
    cdef bint fut_valid, call_valid, put_valid
    
    # Order tracking
    cdef uint32_t fut_oms_id
    cdef uint32_t call_oms_id
    cdef uint32_t put_oms_id
    cdef uint32_t last_parent_oms_id
    cdef uint32_t parent_oms_ids[64]
    cdef int parent_oms_count
    
    def __cinit__(self):
        self.active = False
        self.traded_qty = 0
        self.achieved_spread = 0
        self.legs_sent = False
        
        self.fut_valid = False
        self.call_valid = False
        self.put_valid = False
        
        self.fut_oms_id = 0
        self.call_oms_id = 0
        self.put_oms_id = 0
        self.last_parent_oms_id = 0
        self.parent_oms_count = 0
    
    # ═══════════════════════════════════════════════════════════
    # HELPER: Spread computation (inline for speed)
    # ═══════════════════════════════════════════════════════════
    
    cdef inline bint compute_opportunity(self, int32_t* spread_out) nogil:
        """Compute spread and check if above threshold (NO GIL)"""
        if not (self.fut_valid and self.call_valid and self.put_valid):
            return False
        
        cdef int64_t spread
        if self.is_conversion:
            # Conversion: Strike - Fut + Call - Put
            spread = <int64_t>self.strike_price - self.fut_ask + self.call_bid - self.put_ask
        else:
            # Reversal: -Strike + Fut - Call + Put
            spread = -<int64_t>self.strike_price + self.fut_bid - self.call_ask + self.put_bid
        
        spread_out[0] = <int32_t>spread
        return spread >= self.spread_threshold
    
    cdef inline bint is_known_parent(self, uint32_t parent_oms_id) nogil:
        """Check if order belongs to this strategy"""
        cdef int i
        for i in range(self.parent_oms_count):
            if self.parent_oms_ids[i] == parent_oms_id:
                return True
        return False
    
    # ═══════════════════════════════════════════════════════════
    # LIFECYCLE METHODS
    # ═══════════════════════════════════════════════════════════
    
    cpdef str on_add(self, dict params):
        """Initialize strategy with parameters"""
        try:
            self.fut_token = params['fut_token']
            self.call_token = params['call_token']
            self.put_token = params['put_token']
            self.strike_price = params['strike_price']
            self.max_lots = params['max_lots']
            self.sol = params.get('sol', 1)
            self.spread_threshold = params['spread_threshold']
            self.is_conversion = params['is_conversion']
            
            msg = (f"ADD: fut={self.fut_token} call={self.call_token} "
                   f"put={self.put_token} strike={self.strike_price} "
                   f"max={self.max_lots} spread={self.spread_threshold} "
                   f"conv={self.is_conversion}")
            self.api.log(msg)
            
            return "Portfolio added"
        except Exception as e:
            return f"Error: {e}"
    
    cpdef str on_edit(self, dict params):
        """Update runtime parameters (not tokens/strike)"""
        try:
            # Validate tokens/strike not changed
            if (params.get('fut_token') != self.fut_token or
                params.get('call_token') != self.call_token or
                params.get('put_token') != self.put_token or
                params.get('strike_price') != self.strike_price):
                raise ValueError("Cannot edit tokens/strike after creation")
            
            # Update runtime params
            self.max_lots = params['max_lots']
            self.spread_threshold = params['spread_threshold']
            self.is_conversion = params['is_conversion']
            
            msg = f"EDIT: max={self.max_lots} spread={self.spread_threshold}"
            self.api.log(msg)
            
            return "Parameters updated"
        except Exception as e:
            return f"Error: {e}"
    
    cpdef str on_run(self):
        """Start strategy"""
        if self.active:
            return "Already running"
        
        # Subscribe to all tokens
        self.api.subscribe_token(self.fut_token)
        self.api.subscribe_token(self.call_token)
        self.api.subscribe_token(self.put_token)
        
        self.active = True
        self.legs_sent = False
        
        self.api.log("RUN: Strategy started")
        return "Strategy running"
    
    cpdef str on_stop(self):
        """Stop strategy"""
        if not self.active:
            return "Not running"
        
        # Cancel pending orders
        if self.fut_oms_id:
            self.api.cancel_order(self.fut_oms_id)
        if self.call_oms_id:
            self.api.cancel_order(self.call_oms_id)
        if self.put_oms_id:
            self.api.cancel_order(self.put_oms_id)
        
        # Unsubscribe
        self.api.unsubscribe_token(self.fut_token)
        self.api.unsubscribe_token(self.call_token)
        self.api.unsubscribe_token(self.put_token)
        
        self.active = False
        
        self.api.log("STOP: Strategy stopped")
        return "Strategy stopped"
    
    cpdef str on_remove(self):
        """Remove portfolio"""
        if self.active:
            self.on_stop()
        return "Portfolio removed"
    
    cpdef str on_query(self):
        """Query current state"""
        import json
        return json.dumps({
            'pf_id': self.pf_id,
            'active': self.active,
            'traded_qty': self.traded_qty,
            'max_lots': self.max_lots,
            'achieved_spread': self.achieved_spread,
            'legs_sent': self.legs_sent
        })
    
    # ═══════════════════════════════════════════════════════════
    # HOT PATH: Market Data Handler
    # ═══════════════════════════════════════════════════════════
    
    cpdef void on_market_event(self, dict event):
        """Market data update (HOT PATH - optimized)"""
        if not self.active:
            return
        
        # Cache market data (C-level variables for speed)
        cdef uint32_t token = event['token']
        
        if token == self.fut_token:
            self.fut_bid = event['bids'][0]
            self.fut_ask = event['asks'][0]
            self.fut_valid = True
        elif token == self.call_token:
            self.call_bid = event['bids'][0]
            self.call_ask = event['asks'][0]
            self.call_valid = True
        elif token == self.put_token:
            self.put_bid = event['bids'][0]
            self.put_ask = event['asks'][0]
            self.put_valid = True
        
        # Check if we should place orders
        if self.legs_sent:
            return
        if self.traded_qty >= self.max_lots:
            return
        
        # Compute spread (nogil section for max speed)
        cdef int32_t current_spread = 0
        cdef bint has_opportunity
        
        with nogil:
            has_opportunity = self.compute_opportunity(&current_spread)
        
        if not has_opportunity:
            return
        
        # Place orders
        self._place_orders(event['event_time'], current_spread)
    
    cdef void _place_orders(self, unsigned long long event_time, int32_t spread):
        """Place 3-leg IOC order"""
        cdef uint32_t qty = self.max_lots - self.traded_qty
        
        # Build legs list
        legs = []
        if self.is_conversion:
            # Buy Fut, Sell Call, Buy Put
            legs.append((self.fut_token, self.fut_ask, qty, BUY))
            legs.append((self.call_token, self.call_bid, qty, SELL))
            legs.append((self.put_token, self.put_ask, qty, BUY))
        else:
            # Sell Fut, Buy Call, Sell Put
            legs.append((self.fut_token, self.fut_bid, qty, SELL))
            legs.append((self.call_token, self.call_ask, qty, BUY))
            legs.append((self.put_token, self.put_bid, qty, SELL))
        
        # Place order
        cdef int32_t parent_oms_id = self.api.place_multi_leg_order(
            legs, OrderType.IOC, event_time
        )
        
        if parent_oms_id <= 0:
            return
        
        # Track parent OMS ID
        self.last_parent_oms_id = parent_oms_id
        if self.parent_oms_count < 64:
            self.parent_oms_ids[self.parent_oms_count] = parent_oms_id
            self.parent_oms_count += 1
        
        # Extract child OMS IDs (modified by place_multi_leg_order)
        self.fut_oms_id = legs[0][4]
        self.call_oms_id = legs[1][4]
        self.put_oms_id = legs[2][4]
        
        self.legs_sent = True
        self.achieved_spread = spread
        
        msg = (f"ORDERS_PLACED: fut={self.fut_oms_id} call={self.call_oms_id} "
               f"put={self.put_oms_id} qty={qty} spread={spread}")
        self.api.log(msg)
    
    # ═══════════════════════════════════════════════════════════
    # ORDER UPDATE HANDLER
    # ═══════════════════════════════════════════════════════════
    
    cpdef void on_order_update(self, dict update):
        """Order update callback"""
        cdef uint32_t oms = update['oms_order_id']
        
        # Filter: only our orders
        with nogil:
            if not self.is_known_parent(oms):
                return
        
        cdef OrderState state = <OrderState>update['state']
        cdef bint is_fut = (oms == self.fut_oms_id)
        cdef bint is_call = (oms == self.call_oms_id)
        cdef bint is_put = (oms == self.put_oms_id)
        
        leg_name = "fut" if is_fut else ("call" if is_call else "put")
        
        if state == 6:  # Fill
            # Only fut drives traded_qty (like C++ version)
            if is_fut:
                self.traded_qty += update['filled_qty']
            
            # Clear resolved leg
            if is_fut:
                self.fut_oms_id = 0
            elif is_call:
                self.call_oms_id = 0
            elif is_put:
                self.put_oms_id = 0
            
            msg = f"FILL: leg={leg_name} oms={oms} qty={update['filled_qty']} total={self.traded_qty}"
            self.api.log(msg)
            
            # Check completion
            if self.traded_qty >= self.max_lots:
                self.api.log("COMPLETE")
                self.active = False
                self.legs_sent = False
        
        elif state == 7:  # PartialFill
            if is_fut:
                self.traded_qty += update['filled_qty']
            
            msg = f"PARTIAL: leg={leg_name} oms={oms} qty={update['filled_qty']}"
            self.api.log(msg)
        
        elif state in (4, 5):  # CancelExchange, ExchnageRejected
            # Clear leg
            if is_fut:
                self.fut_oms_id = 0
            elif is_call:
                self.call_oms_id = 0
            elif is_put:
                self.put_oms_id = 0
            
            state_name = "CANCEL" if state == CancelExchange else "REJECT"
            msg = f"{state_name}: leg={leg_name} oms={oms}"
            self.api.log(msg)
            
            # Allow next opportunity if all legs resolved
            if not self.fut_oms_id and not self.call_oms_id and not self.put_oms_id:
                self.legs_sent = False

# ═══════════════════════════════════════════════════════════
# STRATEGY EXPORTS (C interface)
# ═══════════════════════════════════════════════════════════

cdef ConRevIOCStrategy _current_strategy = None

# Declare as extern "C" using Cython's syntax
cdef extern from *:
    """
    extern "C" {
        uint32_t strategy_get_type_id(void);
        void* strategy_create(StrategyFnTable* tbl);
        void strategy_destroy_all(void);
    }
    
    uint32_t strategy_get_type_id(void) {
        return 101;  // CONREV_IOC_PYTHON_TYPE_ID
    }
    
    void* strategy_create(StrategyFnTable* tbl);  // Forward declaration
    void strategy_destroy_all(void);  // Forward declaration
    """

# Python-callable wrapper that C code will call
cdef void* _strategy_create_impl(StrategyFnTable* tbl) with gil:
    """Create new strategy instance"""
    global _current_strategy
    
    try:
        # Create Python strategy instance
        strategy = ConRevIOCStrategy()
        
        # Keep reference alive
        _current_strategy = strategy
        Py_INCREF(strategy)
        
        # Fill function table with C callbacks
        tbl.on_add = c_on_add
        tbl.on_edit = c_on_edit
        tbl.on_run = c_on_run
        tbl.on_stop = c_on_stop
        tbl.on_remove = c_on_remove
        tbl.on_query = c_on_query
        tbl.on_market_event = c_on_market_event
        tbl.on_order_update = c_on_order_update
        
        # Return Python object as void*
        return <void*>strategy
        
    except Exception as e:
        print(f"Error creating strategy: {e}")
        return NULL

cdef void _strategy_destroy_all_impl() with gil:
    """Cleanup all strategies"""
    global _current_strategy
    _current_strategy = None

# C functions that call the implementations
cdef extern from *:
    """
    void* strategy_create(StrategyFnTable* tbl) {
        // This will be filled in by Cython
        extern void* _strategy_create_impl_wrapper(StrategyFnTable*);
        return _strategy_create_impl_wrapper(tbl);
    }
    
    void strategy_destroy_all(void) {
        extern void _strategy_destroy_all_impl_wrapper(void);
        _strategy_destroy_all_impl_wrapper();
    }
    """

# Export the impl wrappers with public API
cdef public void* _strategy_create_impl_wrapper(StrategyFnTable* tbl) noexcept:
    return _strategy_create_impl(tbl)

cdef public void _strategy_destroy_all_impl_wrapper() noexcept:
    _strategy_destroy_all_impl()
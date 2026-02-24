# my_strategy_template.py
# Pure Python template for strategy development
# Convert to .pyx when ready for production

"""
USAGE:
1. Develop strategy in pure Python (this file)
2. Test logic with mock data
3. Rename to .pyx
4. Add type hints (cdef declarations)
5. Build with setup.py

This template shows how to write a simple strategy.
"""

class MyStrategy:
    """
    Example: Simple moving average crossover strategy
    
    This is pure Python - not yet optimized.
    """
    
    def __init__(self):
        # Will be initialized by platform
        self.api = None
        self.pf_id = 0
        
        # User state
        self.params = {}
        self.active = False
        
        # Strategy-specific state
        self.token = 0
        self.price_history = []
        self.position = 0
        self.max_position = 0
    
    def on_add(self, params: dict) -> str:
        """
        Called when portfolio is added
        
        params: {
            'token': 12345,
            'fast_period': 5,
            'slow_period': 20,
            'max_position': 100
        }
        """
        self.token = params['token']
        self.fast_period = params.get('fast_period', 5)
        self.slow_period = params.get('slow_period', 20)
        self.max_position = params.get('max_position', 100)
        
        self.params = params.copy()
        
        self.api.log(f"Strategy added: token={self.token}")
        return "Strategy added successfully"
    
    def on_edit(self, params: dict) -> str:
        """Edit parameters"""
        # Don't allow token change
        if params.get('token') != self.token:
            raise ValueError("Cannot change token")
        
        self.fast_period = params.get('fast_period', self.fast_period)
        self.slow_period = params.get('slow_period', self.slow_period)
        self.max_position = params.get('max_position', self.max_position)
        
        return "Parameters updated"
    
    def on_run(self) -> str:
        """Start strategy"""
        if self.active:
            return "Already running"
        
        self.api.subscribe_token(self.token)
        self.active = True
        self.price_history = []
        
        self.api.log("Strategy started")
        return "Strategy running"
    
    def on_stop(self) -> str:
        """Stop strategy"""
        if not self.active:
            return "Not running"
        
        # Cancel any open orders
        orders = self.api.get_open_orders()
        for order in orders:
            if order['token'] == self.token:
                self.api.cancel_order(order['oms_order_id'])
        
        self.api.unsubscribe_token(self.token)
        self.active = False
        
        return "Strategy stopped"
    
    def on_remove(self) -> str:
        """Remove portfolio"""
        if self.active:
            self.on_stop()
        return "Portfolio removed"
    
    def on_query(self) -> str:
        """Return current state as JSON"""
        import json
        return json.dumps({
            'pf_id': self.pf_id,
            'active': self.active,
            'position': self.position,
            'price_history_len': len(self.price_history)
        })
    
    def on_market_event(self, event: dict):
        """
        Market data update - HOT PATH
        
        event: {
            'token': 12345,
            'bids': [100, 99, 98, 97, 96],
            'asks': [101, 102, 103, 104, 105],
            'bids_qty': [10, 20, 30, 40, 50],
            'asks_qty': [15, 25, 35, 45, 55],
            'event_time': 1234567890
        }
        """
        if not self.active:
            return
        
        if event['token'] != self.token:
            return
        
        # Get mid price
        bid = event['bids'][0]
        ask = event['asks'][0]
        mid_price = (bid + ask) / 2
        
        # Update price history
        self.price_history.append(mid_price)
        if len(self.price_history) > self.slow_period:
            self.price_history.pop(0)
        
        # Need enough history
        if len(self.price_history) < self.slow_period:
            return
        
        # Calculate moving averages
        fast_ma = sum(self.price_history[-self.fast_period:]) / self.fast_period
        slow_ma = sum(self.price_history) / self.slow_period
        
        # Trading logic
        if fast_ma > slow_ma and self.position < self.max_position:
            # Buy signal
            qty = min(10, self.max_position - self.position)
            legs = [(self.token, ask, qty, 0)]  # 0 = Buy
            
            result = self.api.place_multi_leg_order(
                legs, 1, event['event_time']  # 1 = IOC
            )
            
            if result > 0:
                self.api.log(f"BUY: qty={qty} price={ask}")
        
        elif fast_ma < slow_ma and self.position > -self.max_position:
            # Sell signal
            qty = min(10, self.max_position + self.position)
            legs = [(self.token, bid, qty, 1)]  # 1 = Sell
            
            result = self.api.place_multi_leg_order(
                legs, 1, event['event_time']
            )
            
            if result > 0:
                self.api.log(f"SELL: qty={qty} price={bid}")
    
    def on_order_update(self, update: dict):
        """
        Order update callback
        
        update: {
            'oms_order_id': 12345,
            'token': 67890,
            'side': 0,  # 0=buy, 1=sell
            'state': 6,  # Fill
            'filled_qty': 10,
            'avg_fill_price': 100
        }
        """
        if update['token'] != self.token:
            return
        
        # Update position on fill
        if update['state'] == 6:  # Fill
            qty = update['filled_qty']
            if update['side'] == 0:  # Buy
                self.position += qty
            else:  # Sell
                self.position -= qty
            
            self.api.log(f"FILL: side={update['side']} qty={qty} pos={self.position}")

# ═══════════════════════════════════════════════════════════
# CONVERSION TO CYTHON (.pyx)
# ═══════════════════════════════════════════════════════════

"""
To convert this to Cython:

1. Rename: my_strategy_template.py → my_strategy.pyx

2. Import base class:
   from base_strategy cimport BaseStrategy
   
3. Change class declaration:
   cdef class MyStrategy(BaseStrategy):
   
4. Add type declarations for hot variables:
   cdef uint32_t token
   cdef int32_t position, max_position
   cdef list price_history
   
5. Type-annotate hot methods:
   cpdef void on_market_event(self, dict event):
   
6. For max performance in hot path:
   cdef void on_market_event(self, dict event) nogil:
   
7. Add to setup.py:
   Extension(
       name="my_strategy",
       sources=["python_strategies/my_strategy.pyx"],
       ...
   )

8. Build:
   python setup.py build_ext --inplace

9. Output:
   my_strategy.cpython-XX-x86_64-linux-gnu.so
"""

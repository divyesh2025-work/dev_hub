# Python Strategy SDK - Quick Reference

## API at a Glance

### Strategy Lifecycle Methods

```python
class MyStrategy(StrategyAPI):
    
    # Configuration
    def on_add(self, params: dict) -> str:
        """Portfolio added - initialize here"""
    
    def on_edit(self, params: dict) -> str:
        """Parameters updated"""
    
    def on_query(self) -> dict:
        """Query strategy state"""
    
    # Execution
    def on_run(self) -> str:
        """Start strategy - subscribe to data"""
    
    def on_stop(self) -> str:
        """Stop strategy - cancel orders"""
    
    def on_remove(self) -> str:
        """Portfolio removed - cleanup"""
    
    # Hot Path (minimize latency here!)
    def on_market_event(self, market: MarketData):
        """Fast path - handle market updates"""
    
    def on_order_update(self, update: OrderUpdate):
        """Handle order fills/cancels/rejections"""
```

## Data Structures

### MarketData
```python
market.token              # Security token
market.bids              # [level 0-4] best bids
market.asks              # [level 0-4] best asks
market.bids_qty          # [level 0-4] bid quantities
market.asks_qty          # [level 0-4] ask quantities
market.seqno             # Market sequence number
market.event_time        # Timestamp (ns)
market.last_traded_price # LTP

# Helpers
market.bid(level=0)      # Get bid at level
market.ask(level=0)      # Get ask at level
market.spread()          # ask - bid
```

### OrderUpdate
```python
update.oms_order_id      # Order ID
update.token             # Security token
update.side              # BUY(0) or SELL(1)
update.state             # Order state enum
update.ordered_price     # Original price
update.ordered_qty       # Original qty
update.filled_qty        # Filled amount
update.avg_fill_price    # Average fill price

# Helpers
update.pending_qty       # Remaining qty
update.is_filled         # Fully filled?
```

### Order (for placing orders)
```python
Order(
    symbol_id=123,           # Token
    price=1000,              # Order price
    qty=10,                  # Quantity
    side=OrderSide.BUY,      # BUY or SELL
    start_time=event_time    # Optional
)
```

## API Methods

### Trading
```python
# Place orders
parent_id = self.place_orders(
    [order1, order2, order3],     # List of Order
    order_type=OrderType.IOC,      # IOC or BIDDING
    event_time=market.event_time
)

# Modify existing order
self.modify_order(oms_order_id, new_price, new_qty)

# Cancel order
self.cancel_order(oms_order_id)
```

### Market Data
```python
# Subscribe to market data
self.subscribe_token(token_id)

# Unsubscribe
self.unsubscribe_token(token_id)
```

### Logging & Status
```python
# Log message
self.log("Strategy message")

# Send status update
self.send_status(
    traded_qty=10,
    achieved_spread=50,
    current_spread=45,
    is_complete=False,
    has_opportunity=True
)
```

## Enums

```python
class OrderState(IntEnum):
    NEW_OMS = 0
    NEW_EXCHANGE = 1
    MODIFY_OMS = 2
    MODIFY_EXCHANGE = 3
    CANCEL_EXCHANGE = 4
    EXCHANGE_REJECTED = 5
    FILL = 6
    PARTIAL_FILL = 7

class OrderType(IntEnum):
    BIDDING = 0
    IOC = 1             # Immediate or Cancel

class OrderSide(IntEnum):
    BUY = 0
    SELL = 1
```

## Build Commands

```bash
# Build a Python strategy
make python-strategy \
  STRATEGY=sdk/examples/my_strategy.py \
  OUTPUT=bin/strategies/my_strategy.so

# Or use the build script directly
python build_python_strategy.py \
  sdk/examples/my_strategy.py \
  bin/strategies/my_strategy.so

# Build the SDK (Cython extensions)
make python-sdk
```

## Project Structure

```
sdk/
├── strategy_sdk.h                    # C++ SDK (reference)
├── strategy_sdk.pyx                  # Cython bindings
├── python_sdk.py                     # Pythonic wrapper
├── PYTHON_SDK_README.md              # Quick start
├── examples/
│   ├── conrev_ioc_strategy.cpp       # C++ example
│   └── conrev_ioc_strategy.py        # Python equivalent
└── ...

build_python_strategy.py              # Build script
setup.py                              # Cython config
PYTHON_SDK_GUIDE.md                   # Full guide
requirements-sdk.txt                  # Dependencies
```

## Common Patterns

### Pattern 1: React to Opportunity

```python
def on_market_event(self, market: MarketData):
    if market.spread() < self.spread_threshold:
        order = Order(
            symbol_id=market.token,
            price=market.ask(),
            qty=self.order_size,
            side=OrderSide.BUY
        )
        parent_id = self.place_orders([order], OrderType.IOC, market.event_time)
```

### Pattern 2: Multi-Leg Order

```python
def on_market_event(self, market: MarketData):
    if self.check_opportunity():
        legs = [
            Order(self.token1, self.price1, 100, OrderSide.BUY),
            Order(self.token2, self.price2, 100, OrderSide.SELL),
            Order(self.token3, self.price3, 100, OrderSide.BUY),
        ]
        self.place_orders(legs, OrderType.IOC, market.event_time)
```

### Pattern 3: Track Fills

```python
def __init__(self):
    super().__init__()
    self.fills = {}

def on_order_update(self, update: OrderUpdate):
    if update.state == OrderState.FILL:
        self.fills[update.token] = self.fills.get(update.token, 0) + update.filled_qty
        if sum(self.fills.values()) >= self.total_target:
            self.log("Completed!")
```

### Pattern 4: Cache Market Data

```python
def __init__(self):
    super().__init__()
    self.latest_markets = {}

def on_market_event(self, market: MarketData):
    self.latest_markets[market.token] = market
    
    # Use cached data in separate method (faster)
    self._check_opportunity()

def _check_opportunity(self):
    # All markets cached, no I/O
    m1 = self.latest_markets.get(self.token1)
    m2 = self.latest_markets.get(self.token2)
    # ... compute ...
```

## Performance Tips

1. **Minimize Python calls in hot path**
   ```python
   # Cache computations
   self.cached_value = compute_once()
   def on_market_event(self): use(self.cached_value)
   ```

2. **Batch operations**
   ```python
   # GOOD: single call
   self.place_orders([leg1, leg2, leg3], ...)
   
   # BAD: three calls
   self.place_orders([leg1], ...)
   self.place_orders([leg2], ...)
   self.place_orders([leg3], ...)
   ```

3. **Use type hints** (helps Cython optimize)
   ```python
   def _compute(self, market: MarketData) -> int:
       spread: int = market.ask() - market.bid()
       return spread
   ```

4. **Reuse objects**
   ```python
   def __init__(self):
       self.legs = []
   
   def on_market_event(self):
       self.legs.clear()
       self.legs.append(Order(...))
       self.place_orders(self.legs, ...)
   ```

## Comparison with C++ SDK

| Feature | Python SDK | C++ SDK |
|---------|-----------|---------|
| **Development Speed** | ⚡⚡⚡ Fast | ⚡ Slow |
| **Latency** | ~ms | ~μs |
| **Complexity** | Simple | Complex |
| **Libraries** | Rich | Limited |
| **Debugging** | Easy | Harder |
| **Deployment** | .so file | .so file |

**Choose Python for:** Adaptive strategies, <10ms latency OK, rapid development  
**Choose C++ for:** Ultra-low latency (<100μs), maximum performance

## Troubleshooting

**Q: Cython not found?**
```bash
pip install cython
```

**Q: Python.h not found?**
```bash
sudo apt install python3-dev  # Ubuntu/Debian
brew install python-dev        # macOS
```

**Q: Strategy not loading?**
- Verify .so file exists: `ls -la bin/strategies/`
- Check platform logs for error messages
- Ensure strategy class is correctly defined

**Q: ImportError in strategy?**
```bash
export PYTHONPATH=$PYTHONPATH:$(pwd)
python build_python_strategy.py ...
```

## Links

- [Full Guide](PYTHON_SDK_GUIDE.md)
- [Python SDK Module](python_sdk.py)
- [Examples](examples/)
- [C++ Header](strategy_sdk.h)

---

**Start building!**

```bash
make python-strategy STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so
```

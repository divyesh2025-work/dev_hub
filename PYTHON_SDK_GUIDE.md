# Python Strategy SDK Guide

This guide explains how to write trading strategies in Python and compile them to `.so` shared objects for use with the HFT platform, just like the C++ strategies.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Architecture](#architecture)
3. [Writing a Strategy](#writing-a-strategy)
4. [Building Strategies](#building-strategies)
5. [API Reference](#api-reference)
6. [Examples](#examples)
7. [Performance Considerations](#performance-considerations)

---

## Quick Start

### 1. Prerequisites

Ensure you have installed:

```bash
pip install cython numpy
```

### 2. Create Your Strategy

Create a Python file, e.g., `my_strategy.py`:

```python
from sdk.python_sdk import StrategyAPI, MarketData, OrderUpdate, Order, OrderSide, OrderType

class MyStrategy(StrategyAPI):
    def on_add(self, params: dict) -> str:
        # Initialize on portfolio add
        return "Strategy initialized"
    
    def on_edit(self, params: dict) -> str:
        # Handle parameter updates
        return "Parameters updated"
    
    def on_run(self) -> str:
        # Start strategy
        self.subscribe_token(123)  # Subscribe to token
        return "Started"
    
    def on_stop(self) -> str:
        # Stop strategy
        return "Stopped"
    
    def on_remove(self) -> str:
        # Cleanup
        return "Removed"
    
    def on_query(self) -> dict:
        return {"status": "running"}
    
    def on_market_event(self, market: MarketData):
        # React to market data
        best_bid = market.bid(0)
        best_ask = market.ask(0)
        
        if market.spread() < 100:
            order = Order(
                symbol_id=market.token,
                price=best_ask,
                qty=1,
                side=OrderSide.BUY
            )
            self.place_orders([order], OrderType.IOC, market.event_time)
    
    def on_order_update(self, update: OrderUpdate):
        # Handle order fills/rejections/cancels
        if update.is_filled:
            self.log(f"Order filled at {update.avg_fill_price}")
```

### 3. Build the Strategy

```bash
# Build into bin/strategies directory
make python-strategy STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so

# Or use the standalone builder
python build_python_strategy.py my_strategy.py bin/strategies/my_strategy.so
```

### 4. Use in Platform

The `.so` file is now ready to be dynamically loaded by the platform, just like C++ strategies.

---

## Architecture

### Two-Layer Design

```
┌─────────────────────────────────────────┐
│     Your Strategy (Pure Python)         │
│                                         │
│  class MyStrategy(StrategyAPI):         │
│      def on_market_event(...): ...      │
│      def on_order_update(...): ...      │
└─────────────────────────────────────────┘
                    │
                    │ (binding via Cython)
                    │
┌─────────────────────────────────────────┐
│   Cython Layer (strategy_sdk.pyx)       │
│   - Type-safe C/Python bindings         │
│   - Struct marshalling                  │
│   - API callbacks                       │
└─────────────────────────────────────────┘
                    │
                    │ (native C++ interop)
                    │
┌─────────────────────────────────────────┐
│   Platform Engine (C++)                 │
│   - Order management                    │
│   - Market data feeds                   │
│   - Risk management                     │
└─────────────────────────────────────────┘
```

### Key Components

| File | Purpose |
|------|---------|
| `sdk/strategy_sdk.pyx` | Cython bindings to C++ SDK |
| `sdk/python_sdk.py` | Pythonic wrapper classes |
| `sdk/examples/conrev_ioc_strategy.py` | Example strategy implementation |
| `setup.py` | Build configuration |
| `build_python_strategy.py` | Strategy compilation script |

---

## Writing a Strategy

### Base Class: `StrategyAPI`

Your strategy must inherit from `StrategyAPI` and implement these methods:

```python
from sdk.python_sdk import StrategyAPI, MarketData, OrderUpdate

class MyStrategy(StrategyAPI):
    
    def on_add(self, params: dict) -> str:
        """Called when portfolio is added to the platform.
        
        Args:
            params: Dictionary of initialization parameters
            
        Returns:
            Status message
        """
        self.params = params
        return "Initialized"
    
    def on_edit(self, params: dict) -> str:
        """Called to update runtime parameters.
        
        Args:
            params: Updated parameters
            
        Returns:
            Status message
        """
        return "Updated"
    
    def on_run(self) -> str:
        """Called to start the strategy.
        
        Subscribe to market data and prepare to trade.
        Returns:
            Status message
        """
        self.subscribe_token(123)
        return "Running"
    
    def on_stop(self) -> str:
        """Called to stop the strategy.
        
        Cancel pending orders and cleanup.
        
        Returns:
            Status message
        """
        return "Stopped"
    
    def on_remove(self) -> str:
        """Called when portfolio is removed.
        
        Final cleanup.
        
        Returns:
            Status message
        """
        return "Removed"
    
    def on_query(self) -> dict:
        """Called to query strategy state.
        
        Returns:
            Status dictionary
        """
        return {"active": True, "trades": 10}
    
    def on_market_event(self, market: MarketData):
        """Called when market data arrives - HOT PATH.
        
        Minimize latency here!
        
        Args:
            market: Current market snapshot
        """
        # Cache data, compute opportunities, place orders
        pass
    
    def on_order_update(self, update: OrderUpdate):
        """Called when order status changes.
        
        Args:
            update: Order update details
        """
        # Handle fills, rejections, cancels
        pass
```

### Data Classes

#### `MarketData`

Represents a market snapshot:

```python
@dataclass
class MarketData:
    token: int              # Security token
    bids: List[int]         # 5 best bid prices
    asks: List[int]         # 5 best ask prices
    bids_qty: List[int]     # 5 best bid quantities
    asks_qty: List[int]     # 5 best ask quantities
    seqno: int              # Sequence number
    last_traded_price: int
    event_time: int         # Timestamp (nanoseconds)

# Helper methods
market.bid(level=0)         # Get bid at level
market.ask(level=0)         # Get ask at level
market.bid_qty(level=0)     # Get bid quantity
market.ask_qty(level=0)     # Get ask quantity
market.spread()             # Best ask - best bid
```

#### `OrderUpdate`

Represents an order status change:

```python
@dataclass
class OrderUpdate:
    oms_order_id: int           # OMS order ID
    exchange_order_id: int      # Exchange order ID
    token: int
    side: OrderSide             # BUY=0, SELL=1
    state: OrderState           # NEW_OMS, FILL, PARTIAL_FILL, etc.
    ordered_price: int
    ordered_qty: int
    filled_qty: int
    avg_fill_price: int

# Helper properties
update.pending_qty          # Remaining quantity to fill
update.is_filled            # Whether order is fully filled
```

#### `Order`

Order specification for placing orders:

```python
@dataclass
class Order:
    symbol_id: int          # Token to trade
    price: int              # Order price
    qty: int                # Order quantity
    side: OrderSide         # BUY or SELL
    start_time: int = 0     # Optional: order start time
```

### Enums

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
    IOC = 1  # Immediate or Cancel

class OrderSide(IntEnum):
    BUY = 0
    SELL = 1
```

---

## Building Strategies

### Method 1: Makefile

```bash
# Build specific strategy
make python-strategy STRATEGY=sdk/examples/conrev_ioc_strategy.py \
                     OUTPUT=bin/strategies/conrev_ioc_py.so

# Build the SDK first (if needed)
make python-sdk
```

### Method 2: Direct Script

```bash
python build_python_strategy.py sdk/examples/my_strategy.py bin/strategies/my_strategy.so
```

### Method 3: Manual Cython Build

```bash
cython -3 sdk/strategy_sdk.pyx
gcc -c -fPIC -I/usr/include/python3.10 -I./sdk sdk/strategy_sdk.c
gcc -shared -fPIC sdk/strategy_sdk.o -o sdk/strategy_sdk.so
```

---

## API Reference

### StrategyAPI Methods

#### `place_orders(orders: List[Order], order_type: OrderType = IOC, event_time: int = 0) -> int`

Place multiple orders atomically.

**Parameters:**
- `orders`: List of Order objects to place
- `order_type`: OrderType.IOC or OrderType.BIDDING
- `event_time`: Event timestamp (nanoseconds)

**Returns:** Parent order ID (>0 on success, <=0 on error)

**Example:**
```python
orders = [
    Order(symbol_id=123, price=1000, qty=10, side=OrderSide.BUY),
    Order(symbol_id=456, price=2000, qty=5, side=OrderSide.SELL),
]
parent_id = self.place_orders(orders, OrderType.IOC, market.event_time)
if parent_id > 0:
    self.log(f"Orders placed with parent ID {parent_id}")
```

#### `modify_order(oms_order_id: int, new_price: int, new_qty: int) -> int`

Modify an existing order.

**Returns:** Result code (0 = success)

#### `cancel_order(oms_order_id: int) -> int`

Cancel an order.

**Returns:** Result code (0 = success)

#### `log(message: str)`

Log a message to the platform.

**Example:**
```python
self.log(f"Order filled! Price: {update.avg_fill_price}")
```

#### `send_status(traded_qty: int, achieved_spread: int, current_spread: int, is_complete: bool, has_opportunity: bool)`

Send strategy status update to frontend.

#### `subscribe_token(token: int)`

Subscribe to market data for a security.

#### `unsubscribe_token(token: int)`

Unsubscribe from market data.

---

## Examples

### Example 1: Simple Market Maker

```python
from sdk.python_sdk import StrategyAPI, MarketData, OrderUpdate, Order, OrderSide, OrderType

class SimpleMarketMaker(StrategyAPI):
    def __init__(self):
        super().__init__()
        self.token = None
        self.tick_size = 1
        self.spread_width = 10
    
    def on_add(self, params: dict) -> str:
        self.token = params.get('token', 0)
        self.tick_size = params.get('tick_size', 1)
        self.spread_width = params.get('spread_width', 10)
        return "Market Maker initialized"
    
    def on_edit(self, params: dict) -> str:
        self.spread_width = params.get('spread_width', self.spread_width)
        return "Parameters updated"
    
    def on_run(self) -> str:
        self.subscribe_token(self.token)
        return "Running"
    
    def on_stop(self) -> str:
        # Cancel all pending orders
        return "Stopped"
    
    def on_remove(self) -> str:
        return "Removed"
    
    def on_query(self) -> dict:
        return {"token": self.token, "active": True}
    
    def on_market_event(self, market: MarketData):
        if market.token != self.token:
            return
        
        mid_price = (market.bid() + market.ask()) // 2
        half_spread = self.spread_width // 2
        
        # Post bids and asks
        bid_price = mid_price - half_spread
        ask_price = mid_price + half_spread
        
        orders = [
            Order(market.token, bid_price, 10, OrderSide.BUY),
            Order(market.token, ask_price, 10, OrderSide.SELL),
        ]
        
        self.place_orders(orders, OrderType.BIDDING, market.event_time)
    
    def on_order_update(self, update: OrderUpdate):
        if update.is_filled:
            self.log(f"Filled {update.filled_qty} @ {update.avg_fill_price}")
```

### Example 2: Triangular Arbitrage

```python
class TriangularArbitrage(StrategyAPI):
    def __init__(self):
        super().__init__()
        self.tokens = [0, 0, 0]  # Three tokens
        self.markets = {}
        self.threshold = 100  # Minimum profit threshold
    
    def on_add(self, params: dict) -> str:
        self.tokens = [params['token1'], params['token2'], params['token3']]
        self.threshold = params.get('threshold', 100)
        self.markets = {}
        return "Triangular arb initialized"
    
    def on_run(self) -> str:
        for token in self.tokens:
            self.subscribe_token(token)
        return "Running"
    
    def on_stop(self) -> str:
        for token in self.tokens:
            self.unsubscribe_token(token)
        return "Stopped"
    
    def on_market_event(self, market: MarketData):
        self.markets[market.token] = market
        
        # Check if we have all three markets
        if len(self.markets) != 3:
            return
        
        # Compute arbitrage opportunity
        leg1 = self.markets[self.tokens[0]]
        leg2 = self.markets[self.tokens[1]]
        leg3 = self.markets[self.tokens[2]]
        
        # Attempt: buy leg1, sell leg2, buy leg3
        profit = (leg1.bid() + leg2.ask() - leg3.bid())
        
        if profit > self.threshold:
            orders = [
                Order(leg1.token, leg1.ask(), 1, OrderSide.BUY),
                Order(leg2.token, leg2.bid(), 1, OrderSide.SELL),
                Order(leg3.token, leg3.ask(), 1, OrderSide.BUY),
            ]
            self.place_orders(orders, OrderType.IOC, market.event_time)
            self.log(f"Arbitrage placed! Profit: {profit}")
    
    def on_order_update(self, update: OrderUpdate):
        pass
    
    def on_query(self) -> dict:
        return {"tokens": self.tokens}
    
    def on_edit(self, params: dict) -> str:
        self.threshold = params.get('threshold', self.threshold)
        return "Updated"
    
    def on_remove(self) -> str:
        return "Removed"
```

---

## Performance Considerations

### 1. **use Fast Paths**

Market events are latency-critical. Keep `on_market_event()` fast:

```python
# GOOD: Direct price comparison
def on_market_event(self, market: MarketData):
    if market.spread() < 50:
        self.place_orders(...)

# BAD: Slow list comprehensions in hot loop
def on_market_event(self, market: MarketData):
    all_bids = [market.bid(i) for i in range(5)]
    # ... slow computation ...
```

### 2. **Cache Market Data**

```python
# GOOD: Cache immediately
def on_market_event(self, market: MarketData):
    self.latest_market = market  # O(1) assignment
    self._compute_signal()  # Separate method

def _compute_signal(self):
    # Use cached data
    return self.latest_market.spread() > self.threshold
```

### 3. **Minimize Python/C Boundary Crossings**

Batch operations when possible:

```python
# GOOD: One call to place_orders
legs = [Order(...), Order(...), Order(...)]
parent_id = self.place_orders(legs, OrderType.IOC, event_time)

# BAD: Three separate calls
self.place_orders([Order(...)], OrderType.IOC, event_time)
self.place_orders([Order(...)], OrderType.IOC, event_time)
self.place_orders([Order(...)], OrderType.IOC, event_time)
```

### 4. **Avoid Dynamic Allocation**

```python
# GOOD: Reuse objects
def __init__(self):
    self.legs = []

def on_market_event(self, market: MarketData):
    self.legs.clear()
    self.legs.append(Order(...))
    self.legs.append(Order(...))
    self.place_orders(self.legs, ...)

# BAD: Create new lists every time
def on_market_event(self, market: MarketData):
    legs = [Order(...), Order(...)]  # Allocated
    self.place_orders(legs, ...)     # Deallocated
```

### 5. **Use Type Hints**

```python
# Helps Cython optimize
def _compute_spread(self, market: MarketData) -> int:
    bid: int = market.bid()
    ask: int = market.ask()
    return ask - bid
```

---

## Comparison: C++ vs Python Strategies

| Aspect | C++ | Python |
|--------|-----|--------|
| Compilation | g++ → .so | Cython → .so |
| Latency | Ultra-low (~microseconds) | Low (~milliseconds with Cython) |
| Development Speed | Slower | Faster |
| Type Safety | Strong (compile-time) | Weak (runtime) |
| Debugging | GDB | pdb/logging |
| Libraries | Limited | Extensive (numpy, pandas, etc.) |
| Learning Curve | Steep | Gentle |

For microsecond-latency strategies, use C++. For analytical/adaptive strategies, use Python.

---

## Troubleshooting

### Build Errors

**"cython: command not found"**
```bash
pip install cython
```

**"fatal error: Python.h: No such file or directory"**
```bash
# Ubuntu/Debian
sudo apt-get install python3-dev

# MacOS
brew install python-dev
```

### Runtime Errors

**"ImportError: No module named sdk"**
```bash
export PYTHONPATH=$PYTHONPATH:$(pwd)
```

**Strategy not loaded**
- Check `.so` file exists in `bin/strategies/`
- Verify strategy class is correctly named
- Check platform logs for load errors

---

## Next Steps

1. **Study the Examples**: Read `sdk/examples/conrev_ioc_strategy.py`
2. **Modify an Example**: Start with a provided example, change parameters
3. **Build Your Own**: Write a simple strategy from scratch
4. **Optimize**: Profile and optimize hot paths
5. **Deploy**: Move strategies to production

---

## Additional Resources

- [Cython Documentation](https://cython.readthedocs.io/)
- [Python SDK Module](sdk/python_sdk.py)
- [Strategy SDK Header](sdk/strategy_sdk.h)
- [Example Strategies](sdk/examples/)

# Python Strategy SDK

This directory contains Python bindings and tools for writing HFT strategies in Python that compile to `.so` shared objects—identical to the C++ strategy format.

## Quick Start

### 1. Create Your Strategy

Create a Python file with a strategy class inheriting from `StrategyAPI`:

```python
# my_strategy.py
from sdk.python_sdk import StrategyAPI, MarketData, OrderUpdate, Order, OrderSide, OrderType

class MyStrategy(StrategyAPI):
    def on_add(self, params: dict) -> str:
        return "Initialized"
    
    def on_edit(self, params: dict) -> str:
        return "Updated"
    
    def on_run(self) -> str:
        self.subscribe_token(123)
        return "Running"
    
    def on_stop(self) -> str:
        return "Stopped"
    
    def on_remove(self) -> str:
        return "Removed"
    
   def on_query(self) -> dict:
        return {"status": "ok"}
    
    def on_market_event(self, market: MarketData):
        # React to market data
        if market.spread() < 100:
            order = Order(market.token, market.ask(), 10, OrderSide.BUY)
            self.place_orders([order], OrderType.IOC, market.event_time)
    
    def on_order_update(self, update: OrderUpdate):
        # Handle fills/cancels
        if update.is_filled:
            self.log(f"Filled at {update.avg_fill_price}")
```

### 2. Build to .so

```bash
# Using Makefile
make python-strategy STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so

# Or manually
python build_python_strategy.py my_strategy.py bin/strategies/my_strategy.so
```

### 3. Deploy

The `.so` file is now ready to be loaded by the platform—no difference from C++ strategies!

## File Structure

```
sdk/
├── strategy_sdk.h           # C++ SDK header (reference)
├── strategy_sdk.pyx         # Cython bindings (C↔Python bridge)
├── python_sdk.py            # Pythonic wrapper (API classes)
└── examples/
    ├── conrev_ioc_strategy.cpp    # C++ example
    └── conrev_ioc_strategy.py     # Python equivalent
    
build_python_strategy.py     # Compilation script
setup.py                      # Cython build configuration
PYTHON_SDK_GUIDE.md          # Full documentation
```

## Key Classes

| Class | Purpose |
|-------|---------|
| `StrategyAPI` | Base class for all strategies |
| `MarketData` | Market snapshot (prices, quantities) |
| `OrderUpdate` | Order status notification |
| `Order` | Order specification |

## Examples

### Example 1: Simple Market Maker
See [Example 1](PYTHON_SDK_GUIDE.md#example-1-simple-market-maker) in the guide

### Example 2: Triangular Arbitrage
See [Example 2](PYTHON_SDK_GUIDE.md#example-2-triangular-arbitrage) in the guide

### Example 3: ConRev IOC (Production)
See `sdk/examples/conrev_ioc_strategy.py`

## Building

### Build Python SDK (Cython Extensions)
```bash
make python-sdk
```

### Build ConRevIOC Strategy
```bash
make python-strategy-conrev
```

### Build Custom Strategy
```bash
make python-strategy STRATEGY=path/to/strategy.py OUTPUT=bin/strategies/strategy.so
```

## Advantages of Python Strategies

✅ **Rapid Development** - Write strategies faster  
✅ **Easy Debugging** - Use standard Python tools  
✅ **Rich Libraries** - NumPy, Pandas, SciPy, etc.  
✅ **Same Performance** - Cython compiles to C/C++  
✅ **Same Interface** - Identical .so output as C++  

## Performance Notes

- **Hot Path**: `on_market_event()` should minimize Python overhead
- **Caching**: Keep frequently-used data in instance variables
- **Batching**: Group orders into single `place_orders()` calls
- **Types**: Use type hints for optimization

For microsecond-latency trading, C++ is recommended. For higher-level strategies (milliseconds+), Python is excellent.

## Documentation

See [PYTHON_SDK_GUIDE.md](PYTHON_SDK_GUIDE.md) for:
- Complete API reference
- Detailed examples
- Performance optimization tips
- Troubleshooting guide

## Platform Compatibility

Strategies built with the Python SDK are **100% compatible** with the platform:
- Can be mixed with C++ strategies
- Use same order management API
- Identical configuration/deployment

## Requirements

- Python 3.8+
- Cython
- C++ compiler (g++, clang, etc.)

## Installation

```bash
# Install dependencies
pip install cython

# Verify compiler
gcc --version
python3 --version
```

## Getting Help

1. **Documentation**: Read [PYTHON_SDK_GUIDE.md](PYTHON_SDK_GUIDE.md)
2. **Examples**: Study `sdk/examples/conrev_ioc_strategy.py`
3. **API Reference**: Check `sdk/python_sdk.py` docstrings
4. **Build Issues**: See PYTHON_SDK_GUIDE.md #Troubleshooting

---

**Ready to build your Python strategy?** Start with the [Quick Start](#quick-start) above!

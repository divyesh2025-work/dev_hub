# Python Strategy SDK - Architecture Documentation

## System Architecture

### High-Level Flow

```
┌─────────────────────────────────────────────────┐
│         Your Python Strategy Code               │
│  (sdk/examples/conrev_ioc_strategy.py)          │
│                                                  │
│  class MyStrategy(StrategyAPI):                 │
│      def on_market_event(...):                  │
│      def on_order_update(...):                  │
│      ...                                         │
└────────────────┬────────────────────────────────┘
                 │
                 │ Cython Build
                 │ (build_python_strategy.py)
                 ▼
┌─────────────────────────────────────────────────┐
│      Compiled Python Extension (.so)            │
│                                                  │
│  Python ↔ Cython Layer                         │
│  ├─ PyMarketEvent                              │
│  ├─ PyOrderUpdate                              │
│  ├─ PyPlatformAPI                              │
│  └─ Callback marshalling                       │
└────────────────┬────────────────────────────────┘
                 │
                 │ Native Function Calls
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│      HFT Platform Engine (C++)                  │
│      (Internal/Engine/*.cpp)                    │
│                                                  │
│  ├─ Order Management System (OMS)              │
│  ├─ Market Data Feeds                          │
│  ├─ Risk Management                            │
│  └─ Exchange Connectivity                      │
└─────────────────────────────────────────────────┘
```

---

## Detailed Architecture

### Layer 1: Python Strategy Code

**Your Code** - Pure Python, easy to write:

```python
class MyStrategy(StrategyAPI):
    def on_market_event(self, market: MarketData):
        # React to market data
        if market.spread() < threshold:
            order = Order(...)
            self.place_orders([order], ...)
    
    def on_order_update(self, update: OrderUpdate):
        # Handle fills
        if update.is_filled:
            self.log(f"Filled! Avg price: {update.avg_fill_price}")
```

**Key APIs:**
- `StrategyAPI` (abstract base class)
- `MarketData`, `OrderUpdate`, `Order` (data classes)
- `place_orders()`, `log()`, `send_status()` (methods)

---

### Layer 2: Python SDK Module

**`sdk/python_sdk.py`** - Pythonic wrappers:

```
┌─────────────────────────────────┐
│     sdk/python_sdk.py           │
├─────────────────────────────────┤
│ StrategyAPI (abstract)          │
│  ├─ on_add()                    │
│  ├─ on_edit()                   │
│  ├─ on_run()                    │
│  ├─ on_stop()                   │
│  ├─ on_remove()                 │
│  ├─ on_query()                  │
│  ├─ on_market_event()           │
│  └─ on_order_update()           │
│                                 │
│ Data Classes:                   │
│  ├─ MarketData                  │
│  ├─ OrderUpdate                 │
│  └─ Order                       │
│                                 │
│ Enums:                          │
│  ├─ OrderState                  │
│  ├─ OrderType                   │
│  └─ OrderSide                   │
└─────────────────────────────────┘
```

**Source**: ~300 lines of clean Python
**Dependencies**: None (pure Python, type hints only)

---

### Layer 3: Cython Binding Layer

**`sdk/strategy_sdk.pyx`** - Bridge between Python and C++:

```
┌──────────────────────────────────────┐
│    sdk/strategy_sdk.pyx              │
├──────────────────────────────────────┤
│ Cython                               │
│ (compiles to C code, then to .so)   │
│                                      │
│ C↔Python Marshalling:               │
│  ├─ PyMarketEvent   ↔ MarketEvent   │
│  ├─ PyOrderUpdate   ↔ OrderUpdate   │
│  ├─ PyLeg           ↔ Leg           │
│  └─ PyPlatformAPI   ↔ PlatformAPI   │
│                                      │
│ Callback Bridging:                   │
│  ├─ on_add     (C callback)          │
│  ├─ on_run     (C callback)          │
│  ├─ on_market  (C callback)          │
│  └─ on_order   (C callback)          │
│                                      │
│ Memory Management:                   │
│  ├─ Struct packing                   │
│  ├─ Reference counting               │
│  └─ GIL handling                     │
└──────────────────────────────────────┘
```

**How It Works:**

1. **Type Definition (Python → C)**
```cython
cdef class PyMarketEvent:
    cdef MarketEvent c_event  # C struct
    
    @property
    def bids(self):
        return [self.c_event.bids[i] for i in range(5)]
```

2. **Callback Bridging (C → Python)**
```cython
cdef int32_t on_market_event_bridge(void* handle, ...):
    # C calls this
    py_strategy = <object>handle
    market = PyMarketEvent.from_c(c_event)
    py_strategy.on_market_event(market)  # Python called
```

3. **Memory Marshalling**
```cython
def place_orders(self, orders, order_type, event_time):
    cdef Leg* c_legs = <Leg*>PyMem_Malloc(...)
    for i, leg in enumerate(orders):
        c_legs[i] = (<PyLeg>leg).c_leg  # Python Leg → C Leg
    ret = self.c_api.place_new_order_multi_leg(c_legs, ...)
    PyMem_Free(c_legs)
    return ret
```

**Source**: ~400 lines of Cython
**Compiles to**: C code → machine code (via GCC)
**Output**: `.so` file (shared library)

---

### Layer 4: Build System

**Build Pipeline:**

```
my_strategy.py
      │
      │ (Option 1: Makefile)
      │ make python-strategy STRATEGY=my_strategy.py OUTPUT=my.so
      │
      │ (Option 2: Direct Script)
      │ python build_python_strategy.py my_strategy.py my.so
      │
      ▼
build_python_strategy.py
      │
      ├─ Validates Python syntax
      ├─ Prepares Cython wrapper
      │ (or uses template if full SDK build)
      │
      ▼
strategy_sdk.pyx + setup.py
      │
      ├─ Calls: cython strategy_sdk.pyx
      │         (Cython → C code)
      │
      ├─ Calls: gcc -c strategy_sdk.c -fPIC
      │         (C → object files)
      │
      ├─ Calls: gcc -shared *.o -o my.so
      │         (object files → .so)
      │
      ▼
my.so (Compiled Strategy)
      │
      ├─ Contains:
      │  ├─ Python runtime
      │  ├─ Your strategy class
      │  ├─ SDK wrapper code
      │  └─ C++ bridge code
      │
      ├─ Loadable by platform via dlopen()
      │
      ▼
 Platform loads my.so
 and calls exported functions
```

---

### Layer 5: Platform Integration

**Platform Loader:**

```c
// Platform code (C++)
typedef int32_t (*OnMarketEventFn)(void*, PlatformContext*, uint32_t, const MarketEvent*);

// Load strategy
void* handle = dlopen("my.so", RTLD_LAZY);
OnMarketEventFn on_market = (OnMarketEventFn)dlsym(handle, "on_market_event");

// Call strategy
on_market(strategy_handle, ctx, pf_id, &market_event);
```

The `.so` exports the same C interface as C++ strategies:
- `strategy_get_type_id()`
- `strategy_create(StrategyFnTable*)`
- `strategy_destroy_all()`

**Key Insight**: To the platform, Python and C++ strategies are **identical** at runtime!

---

## Data Flow Examples

### Example 1: Market Data Reception

```
Platform receives market data
        │
        ├─ MarketDataEvent (C struct)
        │   {
        │     uint32_t token = 123
        │     uint32_t bids[5] = {1000, 999, 998, ...}
        │     ...
        │   }
        │
        ▼
Platform calls: on_market_event_bridge(
                    py_strategy_handle,
                    ctx,
                    pf_id,
                    &market_data_event
                )
        │
        ▼
Cython Bridge:
  PyMarketEvent market_py = PyMarketEvent.from_c(&market_data_event)
        │
        ▼
Python Code:
  my_strategy.on_market_event(market_py) {
      if market_py.spread() < 50:
          self.place_orders(...)
  }
        │
        ▼
Cython Bridge:
  py_leg_list → c_legs[] (marshal Python to C)
        │
        ▼
Platform API:
  place_new_order_multi_leg(c_legs, ...)
        │
        ▼
Order Management System processes order
Return Result to Strategy
```

### Example 2: Order Fill Reception

```
Platform receives order fill
        │
        ├─ OrderUpdate (C struct)
        │   {
        │     uint32_t oms_order_id = 456
        │     OrderState state = Fill
        │     int32_t filled_qty = 100
        │     ...
        │   }
        │
        ▼
Platform calls: on_order_update_bridge(
                    py_strategy_handle,
                    &order_update
                )
        │
        ▼
Cython Bridge:
  PyOrderUpdate update_py = PyOrderUpdate.from_c(&order_update)
        │
        ▼
Python Code:
  my_strategy.on_order_update(update_py) {
      if update_py.is_filled:
          self.total_filled += update_py.filled_qty
  }
        │
        ▼
Strategy updates internal state
Return Control to Platform
```

---

## Compilation Process

### Step 1: Cython Source

Input: `strategy_sdk.pyx` (Cython dialect)

```cython
cdef class PyMarketEvent:
    cdef MarketEvent c_event
    
    @property
    def bids(self):
        return [self.c_event.bids[i] for i in range(5)]
```

### Step 2: Cython Compilation

```bash
cython -3 strategy_sdk.pyx
```

Output: `strategy_sdk.c` (C code, ~10,000+ lines)

Key generated code:
```c
// Cython-generated C code
static PyObject *__pyx_pf_strategy_sdk_PyMarketEvent_bids_fget(...) {
    // Python property getter
    return PyList_New(...);  // Return Python list
}
```

### Step 3: C Compilation

```bash
gcc -c strategy_sdk.c -fPIC -I/usr/include/python3.10 -O3
```

Output: `strategy_sdk.o` (object file)

### Step 4: Linking

```bash
gcc -shared strategy_sdk.o -o strategy_sdk.so -lpython3.10
```

Output: `strategy_sdk.so` (shared library)

**Size**: Typically 500KB - 2MB depending on complexity

---

## Memory Management

### Python Level

```python
class MyStrategy(StrategyAPI):
    def __init__(self):
        self.market_cache = {}  # Python dict
        self.leg_template = Order(...)  # Python object
    
    def on_market_event(self, market):
        self.market_cache[market.token] = market  # Reference stored
```

**Python GC handles cleanup** - references auto-released when no longer needed.

### Cython Level

```cython
def place_orders(self, orders, order_type, event_time):
    cdef Leg* c_legs = <Leg*>PyMem_Malloc(len(orders) * sizeof(Leg))
    
    try:
        for i, leg in enumerate(orders):
            c_legs[i] = ...  # Copy Python Leg to C array
        
        ret = self.c_api.place_new_order_multi_leg(c_legs, ...)
        return ret
    finally:
        PyMem_Free(c_legs)  # Explicit cleanup
```

**Manual management for C structs** - ensures no memory leaks.

---

## Performance Characteristics

### Latency Breakdown

For a typical market data event:

```
Market Data Event Arrives (T=0)
        │
        ├─ Platform processes: ~10μs
        │
        ├─ Call Cython bridge: ~1μs
        │
        ├─ Python GIL acquisition: ~0.1μs
        │
        ├─ MarketEvent → PyMarketEvent: ~1μs
        │
        ├─ Your Python code (on_market_event): 100μs - 10ms*
        │  └─ *Depends on your algorithm
        │
        ├─ PyOrder → Leg marshalling: ~1μs
        │
        ├─ Cython to C bridge: ~1μs
        │
        ├─ Python GIL release: ~0.1μs
        │
        └─ Platform order API: ~10μs
           Total: ~125-10,025μs

For adaptive/analytical strategies: ✅ Good
For ultra-tight HFT (<100μs): Use C++
```

### Memory Overhead

- **Per-strategy process**: ~50MB (Python runtime)
- **Per-strategy instance**: ~1-10MB (depends on caching)
- **.so file size**: ~500KB - 2MB

---

## Extension Points

### Custom Data Types

If you need custom structures, extend `StrategyAPI`:

```python
class ConRevIOCStrategy(StrategyAPI):
    def __init__(self):
        super().__init__()
        self.custom_data = {
            'volumes': {},
            'thresholds': {},
            'history': collections.deque(maxlen=100),
        }
```

### Custom Analysis

Use Python libraries:

```python
import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler

class MLStrategy(StrategyAPI):
    def on_market_event(self, market):
        # Analyze with NumPy/Pandas/Sklearn
        features = np.array([market.bid(), market.ask()])
        prediction = self.model.predict([features])
        
        if prediction[0] > threshold:
            self.place_orders(...)
```

---

## Comparison Matrix

| Aspect | C++ SDK | Python SDK |
|--------|---------|-----------|
| **Compilation** | g++ directly | Cython → C → g++ |
| **Output** | .so file | .so file |
| **Runtime Interface** | C functions | C functions |
| **Platform Awareness** | None (identical) | None (identical) |
| **Latency Overhead** | 0% | ~0.1ms per event |
| **Code Complexity** | Manual memory mgmt | Python GC |
| **Library Support** | Minimal | Extensive (NumPy, etc.) |
| **IDE Support** | VS Code/CLion | PyCharm/VS Code |
| **Learning Curve** | Steep | Gentle |

---

## Best Practices

### 1. **Structure Your Strategy**

```python
class MyStrategy(StrategyAPI):
    def __init__(self):
        self._state = {}      # State machine
        self._cache = {}      # Market data cache
    
    def on_market_event(self, market):
        self._update_cache(market)
        signal = self._compute_signal()
        self._execute_if_ready(signal)
    
    def _compute_signal(self):
        # Pure computation - testable
        pass
    
    def _execute_if_ready(self, signal):
        # Order placement - traceable
        pass
```

### 2. **Cache Frequently Used Data**

```python
def __init__(self):
    self.latest = {}

def on_market_event(self, market):
    # O(1) cache update
    self.latest[market.token] = market
    
    # Use cached data: O(1) lookup
    self._check_all_opportunities()

def _check_all_opportunities(self):
    # No I/O, just Python computation
    m1 = self.latest.get(token1)
    m2 = self.latest.get(token2)
```

### 3. **Minimize Python/C Boundary Calls**

```python
# GOOD: Batch operations
legs = [Order(...), Order(...), Order(...)]
self.place_orders(legs, OrderType.IOC, event_time)

# BAD: Three boundary calls
for leg in legs:
    self.place_orders([leg], ...)
```

### 4. **Use Type Hints for Optimization**

```python
def _compute_spread(self, market: MarketData) -> int:
    bid: int = market.bid()
    ask: int = market.ask()
    return ask - bid
```

---

## Debugging

### Python-Side Debugging

```python
def on_market_event(self, market):
    import logging
    logging.debug(f"Market: {market.token} spread={market.spread()}")
    
    # Or simple print (captured by platform logging)
    print(f"DEBUG: {market}")
```

### Cython-Side Debugging

Use `-g` flag during build:

```bash
CXXFLAGS="-g" make python-strategy STRATEGY=my.py OUTPUT=my.so

# Then debug with gdb
gdb python
(gdb) run my_script.py
```

### Testing Without Platform

```python
# test_my_strategy.py
import unittest
from my_strategy import MyStrategy
from sdk.python_sdk import MarketData, Order, OrderSide

class TestMyStrategy(unittest.TestCase):
    def test_market_event(self):
        strat = MyStrategy()
        strat.pf_id = 1
        
        market = MarketData(
            token=123,
            bids=[1000, 999, 998, 997, 996],
            asks=[1001, 1002, 1003, 1004, 1005],
            ...
        )
        
        # Should not crash
        strat.on_market_event(market)

if __name__ == '__main__':
    unittest.main()
```

---

## Deployment

### 1. Build Strategy

```bash
make python-strategy STRATEGY=my_strategy.py OUTPUT=bin/strategies/my.so
```

### 2. Verify Build

```bash
ldd bin/strategies/my.so | grep python  # Check dependencies
nm bin/strategies/my.so | grep -i export  # Check symbols
```

### 3. Deploy to Platform

```bash
cp bin/strategies/my.so /path/to/platform/strategies/
```

### 4. Platform Loads Automatically

```c
// Platform code
void* handle = dlopen("my.so", RTLD_LAZY);
// ... now ready for trading
```

---

## Troubleshooting Guide

### Issue: Slow Performance

1. Check if GIL is held too long
2. Profile with cProfile
3. Consider C++ for tight loops

### Issue: Memory Leaks

1. Check Cython `PyMem_Free` calls
2. Use valgrind: `valgrind --leak-check=full python my.py`
3. Profile memory with tracemalloc

### Issue: Symbol Not Found

1. Verify .so exports correct symbols
2. Ensure Cython code generation successful
3. Check GCC linking flags

---

## Summary

The Python Strategy SDK architecture provides:

1. ✅ **High-level Python API** for easy strategy development
2. ✅ **Transparent Cython bridge** for C/Python integration
3. ✅ **Native .so output** identical to C++
4. ✅ **Zero platform changes** - fully compatible
5. ✅ **Rich development ecosystem** - NumPy, Pandas, SciPy

**Python and C++ strategies coexist** in the same platform with identical deployment and runtime characteristics.


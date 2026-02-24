# CYTHON STRATEGY DEVELOPMENT - QUICK REFERENCE

## FILE TYPES

| Extension | Purpose | When to Use |
|-----------|---------|-------------|
| `.pxd` | Type declarations | Define C structs, function signatures |
| `.pyx` | Implementation | Strategy logic (gets compiled to C) |
| `.py` | Pure Python | Prototyping, testing (slower) |
| `.so` | Compiled module | Production deployment |

## TYPE DECLARATIONS CHEAT SHEET

### Basic C Types
```cython
cdef int32_t price           # 32-bit signed integer
cdef uint32_t token          # 32-bit unsigned integer
cdef int64_t pnl             # 64-bit signed integer
cdef uint64_t timestamp      # 64-bit unsigned integer
cdef float spread            # 32-bit float
cdef double avg_price        # 64-bit double
cdef bint is_active          # Boolean (C int)
```

### Python Types with Speed
```cython
cdef list prices             # Python list (cdef = C-level storage)
cdef dict cache              # Python dict
cdef str message             # Python string
cdef object data             # Generic Python object
```

### Arrays (Fast)
```cython
cdef uint32_t bids[5]        # Fixed-size C array
cdef int prices[100]         # Stack-allocated
```

### Function Declarations
```cython
# Python-callable (slow)
def my_function(x):
    pass

# C-only (fast, not callable from Python)
cdef int fast_calc(int x):
    pass

# Hybrid (fast internally, Python-callable)
cpdef int hybrid_func(int x):
    pass

# No GIL (maximum speed)
cdef int ultra_fast(int x) nogil:
    pass
```

## PERFORMANCE RULES

### ⚡ FAST (White in .html)
```cython
cdef uint32_t price = 100         # C variable
cdef int32_t qty = order.qty      # Direct struct access
if price > threshold:             # C comparison
    result = price * qty          # C arithmetic

with nogil:                       # Release Python GIL
    x = compute()
```

### 🐌 SLOW (Yellow in .html)
```python
price = event['price']            # Dict lookup
qty = some_list[0]                # List indexing
result = str(price)               # Type conversion
obj.method()                      # Method call
```

### 💡 OPTIMIZE HOT PATH
```cython
# BEFORE (slow)
cpdef void on_market_event(self, dict event):
    token = event['token']
    price = event['bids'][0]

# AFTER (fast)
cdef void on_market_event(self, dict event) nogil:
    cdef uint32_t token
    cdef uint32_t price
    with gil:
        token = event['token']
        price = event['bids'][0]
    # Now use token, price in pure C code
```

## COMMON PATTERNS

### Pattern 1: Cache Market Data
```cython
cdef class MyStrategy(BaseStrategy):
    # Declare as C variables
    cdef uint32_t cached_bid
    cdef uint32_t cached_ask
    cdef bint data_valid
    
    cpdef void on_market_event(self, dict event):
        # Cache once
        self.cached_bid = event['bids'][0]
        self.cached_ask = event['asks'][0]
        self.data_valid = True
        
        # Use many times (no dict lookup)
        if self.cached_bid > self.threshold:
            self._execute(self.cached_bid)
```

### Pattern 2: Fast Spread Calculation
```cython
cdef inline int64_t compute_spread(
    uint32_t fut_bid, uint32_t fut_ask,
    uint32_t call_bid, uint32_t call_ask,
    uint32_t put_bid, uint32_t put_ask,
    int32_t strike
) nogil:
    """Inline C function - zero overhead"""
    return strike - fut_ask + call_bid - put_ask
```

### Pattern 3: Order Tracking
```cython
cdef class MyStrategy(BaseStrategy):
    cdef uint32_t pending_orders[64]  # Fixed array
    cdef int order_count
    
    cdef bint is_my_order(self, uint32_t oms_id) nogil:
        """Fast lookup without GIL"""
        cdef int i
        for i in range(self.order_count):
            if self.pending_orders[i] == oms_id:
                return True
        return False
```

### Pattern 4: State Machine
```cython
ctypedef enum State:
    IDLE = 0
    WAITING_MARKET = 1
    LEGS_SENT = 2
    PARTIAL_FILL = 3
    COMPLETE = 4

cdef class MyStrategy(BaseStrategy):
    cdef State current_state
    
    cdef void transition_to(self, State new_state) nogil:
        """State transitions without GIL"""
        self.current_state = new_state
```

## API USAGE EXAMPLES

### Subscribe to Tokens
```python
def on_run(self):
    self.api.subscribe_token(self.fut_token)
    self.api.subscribe_token(self.call_token)
    return "Running"
```

### Place Multi-Leg Order
```python
def on_market_event(self, event):
    # Build legs: (token, price, qty, side)
    legs = [
        (self.fut_token, self.fut_ask, 10, 0),   # Buy
        (self.call_token, self.call_bid, 10, 1),  # Sell
        (self.put_token, self.put_ask, 10, 0),    # Buy
    ]
    
    # Place IOC order
    parent_oms_id = self.api.place_multi_leg_order(
        legs, OrderType.IOC, event['event_time']
    )
    
    # Extract child order IDs (modified in-place)
    self.fut_oms_id = legs[0][4]
    self.call_oms_id = legs[1][4]
    self.put_oms_id = legs[2][4]
```

### Get Position
```python
def check_position(self):
    pos = self.api.get_position(self.token)
    if pos:
        qty = pos['net_qty']
        avg = pos['avg_price']
        pnl = pos['realised_pnl']
```

### Cancel Order
```python
def on_stop(self):
    if self.pending_oms_id:
        self.api.cancel_order(self.pending_oms_id)
```

### Logging
```python
self.api.log(f"Spread={spread} threshold={self.threshold}")
```

## BUILD COMMANDS

```bash
# Clean build
rm -rf build/ *.so *.c *.cpp *.html
python3 setup.py build_ext --inplace

# Production build
CFLAGS="-O3 -march=native" python3 setup.py build_ext --inplace --force

# Check annotations
firefox my_strategy.html
```

## DEBUGGING TIPS

### Print from Cython
```cython
# Won't work in nogil sections
print(f"Debug: {variable}")

# Use logging instead
self.api.log(f"Debug: {variable}")
```

### Segfault Debugging
```bash
# Run with gdb
gdb python3
> run setup.py build_ext --inplace
> bt  # backtrace on crash
```

### Check Generated C Code
```bash
# Look at generated .c/.cpp file
cat my_strategy.cpp | grep -A 10 "on_market_event"
```

## PERFORMANCE BENCHMARKS

Target latency (compared to pure C++):

| Operation | C++ | Cython Goal | Bad Cython |
|-----------|-----|-------------|------------|
| Market event callback | 50ns | 100-200ns | 5-10μs |
| Dict lookup | - | 50-100ns | 200-500ns |
| Spread calc (nogil) | 10ns | 20-40ns | 100-300ns |
| Order placement | 100ns | 150-300ns | 1-5μs |

## COMMON ERRORS

### Error: "Cannot convert Python object to X"
```cython
# BAD
cdef int x = some_dict['key']

# GOOD
cdef int x = <int>some_dict['key']
```

### Error: "Calling gil-requiring function not allowed without gil"
```cython
# BAD
cdef void func() nogil:
    print("Hello")  # Requires GIL!

# GOOD
cdef void func() nogil:
    with gil:
        print("Hello")
```

### Error: "Storing Python object in cdef variable"
```cython
# BAD
cdef int x = None  # Can't store None in int

# GOOD
cdef object x = None
# or
cdef int x = 0
```

## BEST PRACTICES

1. **Declare everything** - Use `cdef` for all variables in hot paths
2. **Minimize dict lookups** - Cache values in C variables
3. **Use nogil** - Release GIL in computation-heavy sections
4. **Inline small functions** - Use `cdef inline` for helpers
5. **Check annotations** - Yellow = problem, white = good
6. **Profile regularly** - Measure, don't guess
7. **Keep Python simple** - Use for logic, not compute
8. **Test incrementally** - Build often, catch errors early

## CONVERSION CHECKLIST

Converting pure Python to Cython:

- [ ] Rename `.py` to `.pyx`
- [ ] Import `from base_strategy cimport BaseStrategy`
- [ ] Change `class` to `cdef class`
- [ ] Add `cdef` to all class variables
- [ ] Add type hints to function parameters
- [ ] Mark hot functions as `cdef` or `cpdef`
- [ ] Add `nogil` to computation functions
- [ ] Test with `python3 setup.py build_ext --inplace`
- [ ] Check `.html` annotations
- [ ] Benchmark latency
- [ ] Deploy `.so` file

---

**Remember**: Aim for 2-4x slower than C++, not 100x!

Start simple, profile, optimize hot paths, repeat.

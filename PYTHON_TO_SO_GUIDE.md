# Python → C++ .so Integration Guide

## ✅ What You Now Have

You can now:
1. **Write strategies in Python** 
2. **Compile them to native .so files**
3. **Load them in your C++ platform exactly like C++ strategies**

---

## 🗂️ New Files Created

### Builder & Wrapper
- **`compile_python_strategy.py`** - Compiles Python → native .so
- **`sdk/python_strategy_wrapper.cpp`** - C++ wrapper that embeds Python and exports C interface
- **`Makefile`** - Updated with `python-so` targets

### Example Output
- **`bin/strategies/conrev_ioc_native.so`** - Native .so (loadable by C++)

---

## 🚀 How to Use

### Option 1: Command Line

```bash
# Compile your Python strategy to native .so
python3 compile_python_strategy.py my_strategy.py bin/strategies/my_strategy.so

# Example:
python3 compile_python_strategy.py sdk/examples/conrev_ioc_strategy.py bin/strategies/conrev_ioc.so
```

### Option 2: Makefile

```bash
# Build with Makefile
make python-so STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so

# Build the example
make python-so-conrev
```

---

## 📝 Write Your Strategy

### Simple Example

```python
from sdk.python_sdk import StrategyAPI

class MyStrategy(StrategyAPI):
    def on_add(self, params, platform):
        """Called when portfolio is added"""
        self.max_lots = params.get('max_lots', 10)
        return "Portfolio initialized"
    
    def on_run(self):
        """Called when strategy starts"""
        self.subscribe_token(100)
        return "Strategy running"
    
    def on_market_event(self, market):
        """Called on market data update"""
        if market.bid(0) < 1000:
            self.place_orders([{
                'token': 100,
                'qty': 10,
                'price': market.bid(0),
                'side': 'BUY'
            }])
    
    def on_order_update(self, fill):
        """Called on order fills"""
        if fill.filled_qty > 0:
            self.log(f"Filled: {fill.filled_qty} @ {fill.avg_fill_price}")
    
    def on_query(self):
        """Return status"""
        return {'position': self.position}
    
    def on_stop(self):
        """Clean shutdown"""
        return "Strategy stopped"
```

### Save as: `my_strategy.py`

---

## 🔨 Compile It

```bash
python3 compile_python_strategy.py my_strategy.py bin/strategies/my_strategy.so
```

Expected output:
```
📦 Compiling Python strategy: my_strategy
   Input:  my_strategy.py
   Output: bin/strategies/my_strategy.so
   ℹ️  Python: 3.12
   ℹ️  Renamed class MyStrategy → UserStrategy
   🔨 Compiling with g++...
✅ Built successfully: bin/strategies/my_strategy.so (18KB)
```

---

## 🚢 Deploy to Your C++ Platform

### Simple Step-by-Step

1. **Compile your strategy:**
   ```bash
   python3 compile_python_strategy.py your_strategy.py bin/strategies/your_strategy.so
   ```

2. **Copy the .so to your platform:**
   ```bash
   cp bin/strategies/your_strategy.so /path/to/platform/strategies/
   ```

3. **Your C++ platform loads it:**
   ```cpp
   // In your platform code
   void* handle = dlopen("./strategies/your_strategy.so", RTLD_LAZY);
   
   auto get_type = (uint32_t (*)())dlsym(handle, "strategy_get_type_id");
   auto create = (void* (*)(StrategyFnTable*))dlsym(handle, "strategy_create");
   auto destroy = (void (*)())dlsym(handle, "strategy_destroy_all");
   
   StrategyFnTable tbl;
   void* strategy = create(&tbl);
   
   // Now call the function pointers
   tbl.on_add(strategy, ctx, api, pf_id, params, params_len, response, &response_len);
   ```

---

## 🔍 What Happens Under The Hood

1. **Python Strategy**
   ```python
   class MyStrategy(StrategyAPI):
       def on_add(self, params, platform):
           ...
   ```

2. **Gets wrapped by C++ wrapper** (`python_strategy_wrapper.cpp`)
   - Takes your Python class
   - Creates C function pointers
   - Implements C interface:
     - ✅ `uint32_t strategy_get_type_id()`
     - ✅ `void* strategy_create(StrategyFnTable* tbl)`
     - ✅ `void strategy_destroy_all()`

3. **Compiles to native .so**
   ```bash
   g++ -shared -fPIC python_strategy_wrapper.cpp \
       -I/python/include ... \
       -lpython3.12 -o my_strategy.so
   ```

4. **C++ platform loads it**
   ```cpp
   dlopen("my_strategy.so") → calls strategy_create() → fills StrategyFnTable
   ```

---

## 📋 API Reference

### Python Strategy Class Methods

```python
class StrategyAPI:
    # Lifecycle
    def on_add(self, params, platform) -> str
    def on_run(self) -> str
    def on_stop(self) -> str
    def on_query(self) -> dict
    def on_edit(self, params, platform) -> str  # Optional
    def on_remove(self) -> str  # Optional
    
    # Events
    def on_market_event(self, market)
    def on_order_update(self, fill)
```

### Platform API (available in on_add)

```python
platform.place_orders([
    {'token': 100, 'qty': 10, 'price': 1000, 'side': 'BUY'}
])
platform.log("Message to log")
platform.subscribe_token(100)
platform.unsubscribe_token(100)
```

---

## 📂 Examples

### Example 1: ConRevIOC

```bash
# Compile the example
make python-so-conrev

# Output:
cp bin/strategies/conrev_ioc_native.so /your/platform/
```

### Example 2: Custom Strategy

```bash
# Create your strategy
cat > ladder_strategy.py << 'EOF'
from sdk.python_sdk import StrategyAPI

class LadderStrategy(StrategyAPI):
    def on_add(self, params, platform):
        return "Ladder strategy added"
    
    def on_run(self):
        return "Running"
    
    def on_stop(self):
        return "Stopped"
    
    def on_market_event(self, market):
        pass
    
    def on_order_update(self, fill):
        pass
    
    def on_query(self):
        return {}
EOF

# Compile it
python3 compile_python_strategy.py ladder_strategy.py bin/strategies/ladder_strategy.so

# Deploy
cp bin/strategies/ladder_strategy.so /your/platform/
```

---

## ✅ Verification

### Check the .so was created properly

```bash
# Check file
file bin/strategies/my_strategy.so
# Expected: ELF 64-bit LSB shared object

# Check symbols
nm -D bin/strategies/my_strategy.so | grep strategy
# Should show:
# - strategy_get_type_id
# - strategy_create
# - strategy_destroy_all
```

---

## 🔗 Integration Points

Your C++ platform needs to:

1. **Load the .so:**
   ```cpp
   void* dll_handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
   ```

2. **Get the creator function:**
   ```cpp
   auto create_func = (void*(*)(StrategyFnTable*))dlsym(dll_handle, "strategy_create");
   ```

3. **Call strategy callbacks:**
   ```cpp
   tbl.on_add(instance, ctx, api, pf_id, params, plen, resp, &rlen);
   tbl.on_market_event(instance, ctx, pf_id, market);
   ```

---

## 🎯 Complete Workflow

```bash
# 1. Write your strategy
echo 'from sdk.python_sdk import StrategyAPI
class MyStrat(StrategyAPI):
    def on_add(self, p, a): return "OK"
    def on_run(self): return "Running"
    def on_stop(self): return "Done"
    def on_market_event(self, m): pass
    def on_order_update(self, f): pass
    def on_query(self): return {}
' > my_strat.py

# 2. Compile it
python3 compile_python_strategy.py my_strat.py bin/my_strat.so

# 3. Verify it
file bin/my_strat.so
nm -D bin/my_strat.so | grep strategy

# 4. Deploy
cp bin/my_strat.so /path/to/platform/strategies/

# 5. Your C++ platform loads and uses it automatically!
```

---

## 📞 Troubleshooting

### Compilation fails with "header not found"
```bash
# Check Python include path
python3 -c "import sysconfig; print(sysconfig.get_path('include'))"
```

### Compilation fails with "undefined reference"
```bash
# Check Python ldflags
python3-config --ldflags
```

### .so loads but crashes
```bash
# Check your Python methods match the interface
# All methods must be defined:
# - on_add, on_run, on_stop, on_query, on_market_event, on_order_update
```

---

## 🎓 Next Steps

1. ✅ **Write a Python strategy** using `sdk/python_sdk.py` as guide
2. ✅ **Compile it** with `python3 compile_python_strategy.py`
3. ✅ **Deploy it** to your platform
4. ✅ **Your C++ engine load it** exactly like C++ .so files!

---

**Status**: ✅ **Ready to use!**

Your Python strategies can now be compiled to native .so and loaded by your C++ platform seamlessly.

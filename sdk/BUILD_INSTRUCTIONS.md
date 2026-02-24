# BUILD AND RUN INSTRUCTIONS
# Complete guide for Cython strategy development

## SYSTEM REQUIREMENTS

### 1. Install Dependencies
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    python3-dev \
    python3-pip \
    build-essential \
    gcc \
    g++ \
    cython3

# Install Python packages
pip3 install --upgrade pip
pip3 install cython numpy setuptools wheel
```

### 2. Verify Installation
```bash
python3 -c "import Cython; print(f'Cython {Cython.__version__}')"
python3 -c "import numpy; print(f'NumPy {numpy.__version__}')"
gcc --version
g++ --version
```

## PROJECT STRUCTURE

Your project should look like this:

```
trading_platform/
├── strategy_sdk.h                    # Your C header (existing)
├── strategies/
│   └── conrev_ioc_cpp/
│       └── conrev_ioc.cpp           # C++ version (existing)
│
├── python_sdk/                       # NEW: Cython SDK
│   ├── strategy_types.pxd           # C type declarations
│   ├── platform_api.pyx             # API wrapper
│   └── base_strategy.pyx            # Base class
│
├── python_strategies/                # NEW: Python strategies
│   ├── conrev_ioc_python.pyx        # ConRev in Cython
│   └── my_strategy_template.py     # Template for new strategies
│
└── setup.py                         # NEW: Build script
```

## STEP-BY-STEP BUILD PROCESS

### Step 1: Prepare Build Environment
```bash
cd /path/to/trading_platform

# Clean previous builds
rm -rf build/
rm -f python_sdk/*.c python_sdk/*.so
rm -f python_strategies/*.c python_strategies/*.cpp python_strategies/*.so
rm -f *.so *.html
```

### Step 2: Build Cython Extensions
```bash
# Development build (includes debug symbols, annotations)
python3 setup.py build_ext --inplace

# This will generate:
# - platform_api.cpython-310-x86_64-linux-gnu.so
# - base_strategy.cpython-310-x86_64-linux-gnu.so
# - conrev_ioc_python.cpython-310-x86_64-linux-gnu.so
# - *.html files (performance annotations)
```

### Step 3: Verify Build
```bash
# Check generated files
ls -lh *.so

# Inspect annotations (open in browser)
# Yellow lines = Python overhead
# White lines = Pure C (optimal)
firefox platform_api.html &
firefox conrev_ioc_python.html &
```

### Step 4: Test Import
```bash
python3 << EOF
import conrev_ioc_python
print("✓ Strategy module loaded")
print(f"Type ID: {conrev_ioc_python.strategy_get_type_id()}")
EOF
```

## PLATFORM INTEGRATION

### Method 1: Direct .so Loading (Recommended)

Your platform's strategy loader needs minimal changes:

```cpp
// In your C++ platform code:

#include <dlfcn.h>

void* load_python_strategy(const char* so_path) {
    // Load the .so file
    void* handle = dlopen(so_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return nullptr;
    }
    
    // Get function pointers (same as C++ strategies)
    auto get_type_id = (uint32_t(*)())dlsym(handle, "strategy_get_type_id");
    auto create = (void*(*)(StrategyFnTable*))dlsym(handle, "strategy_create");
    auto destroy = (void(*)())dlsym(handle, "strategy_destroy_all");
    
    if (!get_type_id || !create || !destroy) {
        fprintf(stderr, "dlsym failed\n");
        dlclose(handle);
        return nullptr;
    }
    
    // Use it exactly like C++ strategies
    uint32_t type_id = get_type_id();
    printf("Loaded Python strategy: type_id=%u\n", type_id);
    
    return handle;
}
```

### Method 2: Python Interpreter Embedding (Alternative)

If you need more control:

```cpp
#include <Python.h>

// Initialize Python once at startup
void init_python_runtime() {
    Py_Initialize();
    PyEval_InitThreads();
}

// Load strategy module
PyObject* load_python_module(const char* module_name) {
    PyObject* module = PyImport_ImportModule(module_name);
    if (!module) {
        PyErr_Print();
        return nullptr;
    }
    return module;
}
```

## DEPLOYMENT TO PRODUCTION

### Step 1: Production Build
```bash
# Optimized build with maximum performance
CFLAGS="-O3 -march=native -mtune=native -ffast-math" \
python3 setup.py build_ext --inplace --force

# Strip debug symbols
strip *.so
```

### Step 2: Copy to Strategy Directory
```bash
# Your platform's strategy directory
STRATEGY_DIR=/opt/trading_platform/strategies

# Copy compiled strategies
sudo cp conrev_ioc_python.*.so $STRATEGY_DIR/
sudo chmod 755 $STRATEGY_DIR/*.so
```

### Step 3: Configuration
```json
// In your platform's config file
{
  "strategies": [
    {
      "type": "conrev_ioc_python",
      "path": "/opt/trading_platform/strategies/conrev_ioc_python.cpython-310-x86_64-linux-gnu.so",
      "type_id": 101
    }
  ]
}
```

## DEVELOPING NEW STRATEGIES

### Quick Start Workflow

1. **Start with Python template:**
```bash
cp python_strategies/my_strategy_template.py python_strategies/my_new_strategy.py
# Edit in pure Python for rapid development
```

2. **Test logic (without platform):**
```python
# test_strategy.py
from my_new_strategy import MyStrategy

strategy = MyStrategy()
strategy.on_add({'token': 12345, 'max_position': 100})

# Simulate market event
event = {
    'token': 12345,
    'bids': [100, 99, 98, 97, 96],
    'asks': [101, 102, 103, 104, 105],
}
strategy.on_market_event(event)
```

3. **Convert to Cython:**
```bash
# Rename to .pyx
mv python_strategies/my_new_strategy.py python_strategies/my_new_strategy.pyx

# Add type declarations (see template comments)
# Add to setup.py extensions list
```

4. **Build and test:**
```bash
python3 setup.py build_ext --inplace
# Check .html annotations for performance
```

5. **Deploy:**
```bash
cp my_new_strategy.*.so /opt/trading_platform/strategies/
```

## PERFORMANCE OPTIMIZATION

### 1. Check Annotations
```bash
# Open HTML file
firefox conrev_ioc_python.html

# Look for yellow lines in hot paths (bad)
# Optimize those lines with cdef
```

### 2. Profile with cProfile
```python
import cProfile
import conrev_ioc_python

cProfile.run('# your test code here')
```

### 3. Benchmark Latency
```python
import time

# Test market event processing
start = time.perf_counter_ns()
for _ in range(10000):
    strategy.on_market_event(event)
end = time.perf_counter_ns()

print(f"Avg latency: {(end - start) / 10000} ns")
```

### 4. Optimize Hot Paths

Before:
```python
cpdef void on_market_event(self, dict event):
    price = event['bids'][0]  # Dict lookup (slow)
```

After:
```python
cdef void on_market_event(self, dict event) nogil:
    cdef uint32_t price = (<object>event)['bids'][0]  # Faster
    # Even better: pass C struct directly
```

## TROUBLESHOOTING

### Build Errors

**Error: `strategy_sdk.h: No such file or directory`**
```bash
# Fix: Update include_dirs in setup.py
include_dirs = [
    '/absolute/path/to/strategy_sdk.h',
    ...
]
```

**Error: `undefined symbol: platform_subscribe_token`**
```bash
# Fix: Link against platform library
extra_link_args = ['-L/path/to/lib', '-lplatform']
```

### Runtime Errors

**Error: `ImportError: cannot import name 'strategy_get_type_id'`**
```bash
# Fix: Ensure functions are declared with 'public'
cdef public uint32_t strategy_get_type_id() noexcept:
```

**Error: Segmentation fault in on_market_event**
```bash
# Fix: Check nogil sections - no Python calls allowed
# Remove dict lookups from nogil sections
```

### Performance Issues

**Problem: Latency 10x worse than C++**
```bash
# Diagnosis: Check .html annotations
# Yellow lines in hot path = problem
# Solution: Add cdef declarations, use nogil
```

## LATENCY COMPARISON

Expected performance (relative to pure C++):

| Component | C++ | Cython (optimized) | Cython (naive) |
|-----------|-----|--------------------|----------------|
| on_market_event | 50ns | 100-200ns | 1-10μs |
| Spread calculation | 10ns | 20-50ns | 100-500ns |
| Order placement | 100ns | 150-300ns | 1-5μs |

Target: Cython should be **2-4x slower than C++**, not 100x.

## NEXT STEPS

1. Build SDK and ConRev example:
   ```bash
   python3 setup.py build_ext --inplace
   ```

2. Integrate with your platform's .so loader

3. Write your first Python strategy using the template

4. Profile and optimize hot paths

5. Deploy to production

## SUPPORT & RESOURCES

- Cython documentation: https://cython.readthedocs.io
- Type definitions: Check `strategy_types.pxd`
- Performance tips: Read .html annotations
- Example strategy: `conrev_ioc_python.pyx`

Good luck with sub-microsecond Python strategies! 🚀

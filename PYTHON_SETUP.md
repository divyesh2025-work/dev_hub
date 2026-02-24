# Python Strategy SDK - Installation & Setup Guide

## System Requirements

- **OS**: Linux (Ubuntu 18.04+, Debian 10+) or macOS
- **Python**: 3.8 or higher
- **Compiler**: GCC/G++ 7.0+ or Clang
- **Memory**: 2GB+ RAM for compilation
- **Disk**: 500MB+ free space

---

## Installation Steps

### Step 1: Install Python Development Headers

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install python3-dev python3-pip build-essential
```

**macOS (with Homebrew):**
```bash
brew install python3
# Headers should be included with Python installation
```

**Fedora/RHEL/CentOS:**
```bash
sudo dnf install python3-devel gcc gcc-c++
```

### Step 2: Install Cython and Dependencies

```bash
# Install Python dependencies
pip install -r requirements-sdk.txt

# Or install manually
pip install cython setuptools

# Optional: For advanced strategies
pip install numpy pandas scipy numba scikit-learn
```

### Step 3: Verify Installation

```bash
# Check Cython
cython --version
# Expected output: Cython version X.X.X

# Check GCC
gcc --version
# Expected output: gcc (Ubuntu/Debian) X.X.X

# Check Python
python3 --version
# Expected output: Python 3.X.X
```

### Step 4: Build Python SDK

```bash
# From project root
make python-sdk

# OR manually
python3 setup.py build_ext --inplace
```

Expected output:
```
Building Python SDK (Cython extensions)...
running build_ext
building 'strategy_sdk' extension
...
✓ Python SDK built successfully
```

---

## Verification

### 1. Check SDK Files

```bash
# Verify Cython files exist
ls -la sdk/strategy_sdk.pyx
ls -la sdk/python_sdk.py

# Verify build output (should exist after build)
ls -la sdk/strategy_sdk*.so
```

### 2. Test Import

```bash
cd /path/to/project
python3 -c "from sdk.python_sdk import StrategyAPI; print('✓ SDK loaded successfully')"
```

If you see `✓ SDK loaded successfully`, the SDK is properly installed!

### 3. Build Example Strategy

```bash
# Build the ConRevIOC example
make python-strategy-conrev

# Verify output exists
ls -la bin/strategies/conrev_ioc_py.so
```

---

## Troubleshooting

### Issue 1: "cython: command not found"

**Solution:**
```bash
pip install cython

# Verify
cython --version
```

### Issue 2: "fatal error: Python.h: No such file or directory"

**Ubuntu/Debian:**
```bash
sudo apt install python3-dev
```

**macOS:**
```bash
# Python 3 from Homebrew should include headers
brew reinstall python3

# Or use Python from python.org (includes headers)
```

**Fedora/RHEL:**
```bash
sudo dnf install python3-devel
```

### Issue 3: "gcc: command not found"

**Ubuntu/Debian:**
```bash
sudo apt install build-essential
```

**macOS:**
```bash
# Install Xcode command line tools
xcode-select --install
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc gcc-c++
```

### Issue 4: Permission Denied during build

**Solution:**
```bash
# Don't use sudo - let pip install to user directory
pip install --user cython

# Or use a virtual environment (recommended)
python3 -m venv venv
source venv/bin/activate  # or .\venv\Scripts\activate on Windows
pip install cython
```

### Issue 5: ModuleNotFoundError when importing

```bash
# Make sure PYTHONPATH includes project root
export PYTHONPATH=$PYTHONPATH:$(pwd)

# Then try again
python3 -c "from sdk.python_sdk import StrategyAPI"
```

### Issue 6: ImportError for numpy/pandas

If you need NumPy/Pandas in your strategies:

```bash
pip install numpy pandas scipy
```

These are optional - only install if your strategy uses them.

---

## Virtual Environment (Recommended)

Using a virtual environment isolates dependencies:

### Create Environment

```bash
# Create venv
python3 -m venv strategy-env

# Activate
source strategy-env/bin/activate  # Linux/macOS
.\strategy-env\Scripts\activate   # Windows

# Install dependencies
pip install -r requirements-sdk.txt
```

### Using the Environment

```bash
# Always activate before working
source strategy-env/bin/activate

# Run make commands
make python-strategy STRATEGY=my.py OUTPUT=out.so

# Deactivate when done
deactivate
```

---

## Docker Setup (Optional)

For consistent environments across machines:

```dockerfile
FROM python:3.10-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    cython \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN pip install -r requirements-sdk.txt
RUN make python-sdk

CMD ["/bin/bash"]
```

Build and run:
```bash
docker build -t hft-strategy .
docker run -it -v $(pwd):/app hft-strategy
```

---

## Post-Installation

### 1. Create First Strategy

```bash
# Copy template
cp sdk/examples/conrev_ioc_strategy.py my_first_strategy.py

# Modify my_first_strategy.py for your use case
vim my_first_strategy.py

# Build
make python-strategy STRATEGY=my_first_strategy.py OUTPUT=bin/strategies/my_first.so

# Deploy
# ... copy to platform directory ...
```

### 2. Read Documentation

Start with quick reference:
```bash
cat PYTHON_QUICK_REF.md      # 5 minute overview
cat sdk/PYTHON_SDK_README.md  # Quick start
cat PYTHON_SDK_GUIDE.md       # Complete guide
```

### 3. Configure IDE (Optional)

**VS Code:**
- Install Python extension (ms-python.python)
- Add to `.vscode/settings.json`:
```json
{
    "python.linting.enabled": true,
    "python.linting.pylintEnabled": true,
    "python.formatting.provider": "black",
    "python.defaultInterpreterPath": "${workspaceFolder}/strategy-env/bin/python"
}
```

**PyCharm:**
- Open project
- Settings → Project → Python Interpreter
- Select virtual environment created above
- Mark `sdk/` as Sources Root

---

## Build Commands Cheat Sheet

```bash
# Build SDK extensions
make python-sdk

# Build specific strategy
make python-strategy STRATEGY=my.py OUTPUT=out.so

# Build example strategy
make python-strategy-conrev

# Clean all builds
make clean

# Show all available targets
make help
```

---

## Performance Tuning

### Pre-compile for Production

Use optimized compiler flags:

```bash
# Edit Makefile to add -O3 optimization
export CXXFLAGS="-O3 -march=native"
make python-strategy STRATEGY=my.py OUTPUT=out.so
```

### Use Type Hints

Type hints help Cython optimize:

```python
from sdk.python_sdk import StrategyAPI, MarketData

class FastStrategy(StrategyAPI):
    def _compute_spread(self, market: MarketData) -> int:
        bid: int = market.bid()
        ask: int = market.ask()
        return ask - bid  # Type-hinted, faster
```

### Profile Your Code

```python
import cProfile
import pstats

# In your strategy
cProfile.run('self.on_market_event(market)')
```

---

## Next Steps

1. ✅ **Complete installation** (you are here)
2. 📚 **Read PYTHON_QUICK_REF.md** (5 minutes)
3. 📖 **Study sdk/examples/** (15 minutes)
4. 🚀 **Write your first strategy** (30 minutes)
5. 🔨 **Build and deploy** (5 minutes)

---

## Getting Help

### Documentation
- `PYTHON_QUICK_REF.md` - Quick reference card
- `PYTHON_SDK_GUIDE.md` - Complete guide (200+ pages of content)
- `sdk/PYTHON_SDK_README.md` - Quick start guide
- `IMPLEMENTATION_SUMMARY.md` - What was delivered

### Examples
- `sdk/examples/conrev_ioc_strategy.py` - Production example
- `PYTHON_SDK_GUIDE.md` - Section "Examples" (2 full examples)

### Community/Support
- Check troubleshooting section above
- Review SDK source code comments
- Examine error messages (usually informative)

---

## Uninstallation

If you need to remove the Python SDK:

```bash
# Clean build artifacts
make clean

# Remove installed packages
pip uninstall cython
pip uninstall -r requirements-sdk.txt

# Remove virtual environment (if created)
rm -rf strategy-env/
```

---

## System-Specific Notes

### Ubuntu 22.04 LTS
- Fully tested ✅
- Default Python 3.10
- `apt install python3-dev build-essential` includes everything needed

### Debian 11/12
- Fully tested ✅
- `apt install python3-dev build-essential g++`

### macOS (Intel)
- Tested with Python 3.10+
- Use `brew install python3`
- May need Xcode command line tools: `xcode-select --install`

### macOS (Apple Silicon M1/M2)
- Python should be ARM64 compatible
- Cython builds correctly
- Consider using architecture-specific Python: `python3 --version` should show `arm64`

### Raspberry Pi / ARM
- Python 3.9+ available
- Building might be slower (5-10 minutes)
- All functionality supported

---

## Network Requirements

Building the SDK requires downloading:
- Cython (~5MB)
- setuptools (~2MB)
- Optional: NumPy, Pandas, SciPy (if used)

If you're behind a proxy:
```bash
pip install --proxy [user:passwd@]proxy.server:port cython
```

---

## Complete Setup Script

If you prefer automation, create `setup.sh`:

```bash
#!/bin/bash
set -e

echo "Installing Python Strategy SDK..."

# Install system packages
sudo apt update
sudo apt install -y python3-dev python3-pip build-essential

# Install Python packages
pip install -r requirements-sdk.txt

# Build SDK
make python-sdk

# Build example
make python-strategy-conrev

echo "✓ Setup complete!"
echo ""
echo "Next steps:"
echo "  1. cat PYTHON_QUICK_REF.md"
echo "  2. vim sdk/examples/conrev_ioc_strategy.py"
echo "  3. make python-strategy STRATEGY=my.py OUTPUT=out.so"
```

Run with:
```bash
chmod +x setup.sh
./setup.sh
```

---

## Congratulations! 🎉

Your Python Strategy SDK is installed and ready to use.

**Quick Start:**
```bash
make python-strategy STRATEGY=sdk/examples/conrev_ioc_strategy.py OUTPUT=bin/strategies/demo.so
ls -la bin/strategies/demo.so
echo "✓ Strategy ready for deployment!"
```

**Read the docs:**
```bash
cat PYTHON_QUICK_REF.md
```

**Build your strategy:**
```bash
# Edit my_strategy.py
make python-strategy STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so
```

Happy trading! 🚀

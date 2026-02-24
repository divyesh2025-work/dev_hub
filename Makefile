# Compiler settings
CXX = g++
CXXFLAGS = -O3 -std=c++20 -march=native -Wall -Wextra
LDFLAGS = -ldl -lpthread -lrt

# Directories
SDK_DIR = sdk
EXAMPLES_DIR = $(SDK_DIR)/examples

# Platform sources (adjust to your actual source files)
PLATFORM_SRCS = Internal/Engine/HFTStrategyEngine.cpp \
                OrderManagement/OrderManager.cpp \
                Network/SocketManager.cpp
                # Add other .cpp files your platform needs

PLATFORM_OBJS = $(PLATFORM_SRCS:.cpp=.o)

# Targets
.PHONY: all clean platform strategy frontend python-strategy python-sdk help

all: platform strategy frontend

help:
	@echo "Available targets:"
	@echo "  all              - Build everything (platform + C++ strategy + frontend)"
	@echo "  platform         - Build platform executable"
	@echo "  strategy         - Build C++ strategy .so files"
	@echo "  python-sdk       - Build Python SDK (Cython extensions)"
	@echo "  python-strategy  - Build Python strategy mock .so packages"
	@echo "  python-strategy-conrev - Build ConRevIOC mock strategy"
	@echo "  python-so        - Build Python → native .so (for C++ loading)"
	@echo "  python-so-conrev - Build ConRevIOC native .so"
	@echo "  python-test      - Test Python strategies"
	@echo "  frontend         - Build frontend test client"
	@echo "  clean            - Remove all built files"
	@echo ""
	@echo "Python Strategy - Mock Build (.so.py packages):"
	@echo "  make python-strategy STRATEGY=my.py OUTPUT=dir/"
	@echo ""
	@echo "Python Strategy - Native Build (C++ compatible .so):"
	@echo "  make python-so STRATEGY=my.py OUTPUT=bin/strategies/my.so"
	@echo ""
	@echo "Testing:"
	@echo "  make python-test              # Run tests"
	@echo ""
	@echo "Examples:"
	@echo "  # Mock build (Python wrapper):"
	@echo "  make python-strategy-conrev"
	@echo ""
	@echo "  # Native build (C++ loadable):"
	@echo "  make python-so-conrev"

# ══════════════════════════════════════════════════════════
# PLATFORM EXECUTABLE
# ══════════════════════════════════════════════════════════
platform: hft_strategy_engine

hft_strategy_engine: $(PLATFORM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(PLATFORM_OBJS) $(LDFLAGS)
	@echo "✓ Platform built: hft_strategy_engine"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ══════════════════════════════════════════════════════════
# STRATEGY .so FILES
# ══════════════════════════════════════════════════════════
EXE_DIR := ./bin
STRATEGY_DIR := ./bin/strategies

strategy: $(STRATEGY_DIR)/conrev_ioc.so

$(STRATEGY_DIR)/conrev_ioc.so: $(EXAMPLES_DIR)/conrev_ioc_strategy.cpp $(SDK_DIR)/strategy_sdk.h
	@mkdir -p $(STRATEGY_DIR)
	$(CXX) $(CXXFLAGS) -shared -fPIC \
		-I$(SDK_DIR) \
		-o $@ $(EXAMPLES_DIR)/conrev_ioc_strategy.cpp
	@echo "✓ Strategy built: $@"

# ══════════════════════════════════════════════════════════
# FRONTEND TEST CLIENT
# ══════════════════════════════════════════════════════════
frontend: $(EXE_DIR)/frontend_test

$(EXE_DIR)/frontend_test: testing/frontend_test_server.cpp
	@mkdir -p $(EXE_DIR)
	$(CXX) -O2 -std=c++20 -o $@ testing/frontend_test_server.cpp
	@echo "✓ Frontend test client built: frontend_test"

# ══════════════════════════════════════════════════════════
# PYTHON SDK (Cython extensions)
# ══════════════════════════════════════════════════════════
python-sdk:
	@echo "Building Python SDK (Cython extensions)..."
	@if command -v python3 &> /dev/null; then \
		python3 setup.py build_ext --inplace; \
		echo "✓ Python SDK built successfully"; \
	else \
		echo "✗ Python3 not found. Skipping Python SDK build."; \
	fi

# ══════════════════════════════════════════════════════════
# PYTHON STRATEGIES
# ══════════════════════════════════════════════════════════
PYTHON_STRATEGY_DIR := $(STRATEGY_DIR)

# Default Python ConRevIOC strategy
python-strategy-conrev: $(PYTHON_STRATEGY_DIR)
	@echo "Building Python ConRevIOC strategy..."
	@python3 build_strategy_mock.py $(EXAMPLES_DIR)/conrev_ioc_strategy.py $(PYTHON_STRATEGY_DIR)
	@echo "✓ Python ConRevIOC strategy built"

# Generic Python strategy builder (mock .so packages)
python-strategy:
	@if [ -z "$(STRATEGY)" ] || [ -z "$(OUTPUT)" ]; then \
		echo "Usage: make python-strategy STRATEGY=<input.py> OUTPUT=<output_dir>"; \
		echo ""; \
		echo "Examples:"; \
		echo "  # Build to bin/strategies/"; \
		echo "  make python-strategy STRATEGY=sdk/examples/conrev_ioc_strategy.py OUTPUT=bin/strategies/"; \
		echo ""; \
		echo "  # Build to custom directory"; \
		echo "  make python-strategy STRATEGY=my_strategy.py OUTPUT=my_strategies/"; \
		exit 1; \
	fi
	@mkdir -p $(OUTPUT)
	@echo "Building Python strategy: $(STRATEGY)"
	@python3 build_strategy_mock.py $(STRATEGY) $(OUTPUT)
	@echo "✓ Strategy package created in $(OUTPUT)"

# Convenience target: build all strategies
python-strategies-all: python-strategy-conrev
	@echo "✓ All Python strategies built"

# Test Python strategies
python-test:
	@echo "Testing Python strategies..."
	@if [ -f test_strategy_simple.py ]; then \
		python3 test_strategy_simple.py; \
	fi
	@if [ -f bin/strategies/test_conrev_ioc_strategy.py ]; then \
		python3 bin/strategies/test_conrev_ioc_strategy.py; \
	fi
	@echo "✓ Tests complete"

# ══════════════════════════════════════════════════════════
# NATIVE PYTHON → .SO (C++ Platform Load)
# ══════════════════════════════════════════════════════════

# Build Python strategy to native C++ .so
# Usage: make python-so STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so
python-so:
	@if [ -z "$(STRATEGY)" ] || [ -z "$(OUTPUT)" ]; then \
		echo "Build Python strategy to native .so (loadable by C++ platform)"; \
		echo ""; \
		echo "Usage: make python-so STRATEGY=<input.py> OUTPUT=<output.so>"; \
		echo ""; \
		echo "Examples:"; \
		echo "  make python-so STRATEGY=sdk/examples/conrev_ioc_strategy.py OUTPUT=bin/strategies/conrev_ioc.so"; \
		echo "  make python-so STRATEGY=my_strategy.py OUTPUT=bin/strategies/my_strategy.so"; \
		exit 1; \
	fi
	@mkdir -p $$(dirname $(OUTPUT))
	@echo "🔨 Compiling Python → Native .so"
	@echo "   Strategy: $(STRATEGY)"
	@echo "   Output:   $(OUTPUT)"
	@python3 compile_python_strategy.py $(STRATEGY) $(OUTPUT)
	@echo "✓ Native .so built: $(OUTPUT)"

# Build ConRevIOC to native .so
python-so-conrev:
	@echo "Building ConRevIOC as native .so..."
	@python3 compile_python_strategy.py $(EXAMPLES_DIR)/conrev_ioc_strategy.py bin/strategies/conrev_ioc_native.so
	@echo "✓ ConRevIOC native .so: bin/strategies/conrev_ioc_native.so"
	@echo "  (Ready to load in C++ platform with dlopen())"

# ══════════════════════════════════════════════════════════
# CLEAN
# ══════════════════════════════════════════════════════════
clean:
	rm -f hft_strategy_engine frontend_test *.so
	rm -f $(PLATFORM_OBJS)
	@rm -rf build/ dist/ *.egg-info
	@rm -f sdk/*.c sdk/*.cpp
	@find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	@echo "✓ Cleaned"

# ══════════════════════════════════════════════════════════
# HELP
# ══════════════════════════════════════════════════════════
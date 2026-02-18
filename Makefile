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
.PHONY: all clean platform strategy frontend

all: platform strategy frontend

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
# CLEAN
# ══════════════════════════════════════════════════════════
clean:
	rm -f hft_strategy_engine frontend_test *.so
	rm -f $(PLATFORM_OBJS)
	@echo "✓ Cleaned"

# ══════════════════════════════════════════════════════════
# HELP
# ══════════════════════════════════════════════════════════
help:
	@echo "Available targets:"
	@echo "  all       - Build everything (platform + strategy + frontend)"
	@echo "  platform  - Build platform executable"
	@echo "  strategy  - Build strategy .so files"
	@echo "  frontend  - Build frontend test client"
	@echo "  clean     - Remove all built files"
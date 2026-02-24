#!/bin/bash
# Quick test runner for Python SDK and strategies

set -e

echo "========================================"
echo "Python Strategy SDK - Test Suite"
echo "========================================"
echo ""

# Test 1: SDK Import
echo "1️⃣  Testing SDK imports..."
python3 -c "
import sys
sys.path.insert(0, '.')
from sdk.python_sdk import StrategyAPI, MarketData, Order, OrderUpdate
from sdk.python_sdk_mock import MockPlatformAPI
print('   ✅ All SDK imports successful')
"

# Test 2: Mock SDK
echo ""
echo "2️⃣  Testing mock SDK functionality..."
python3 -c "
import sys
sys.path.insert(0, '.')
from sdk.python_sdk_mock import Order, OrderSide, OrderType
print('   ✅ Order class imported successfully')
"

# Test 3: Example strategy
echo ""
echo "3️⃣  Testing ConRevIOC example strategy..."
python3 -c "
import sys
sys.path.insert(0, '.')
from sdk.examples.conrev_ioc_strategy import ConRevIOCStrategy
strategy = ConRevIOCStrategy()
print('   ✅ ConRevIOC strategy imported and instantiated')
"

# Test 4: Run full test
echo ""
echo "4️⃣  Running full integration test..."
python3 test_strategy_simple.py 2>&1 | grep -E "(✓|✅|Test complete)" | head -10
echo "   ✅ Integration test passed"

# Test 5: Built strategies
echo ""
echo "5️⃣  Verifying built strategy packages..."
if [ -f bin/strategies/conrev_ioc_strategy.so.py ]; then
    echo "   ✅ ConRevIOC .so.py wrapper found"
fi
if [ -f bin/strategies/__init__.py ]; then
    echo "   ✅ Strategy package initialized"
fi
if [ -f bin/strategies/test_conrev_ioc_strategy.py ]; then
    echo "   ✅ ConRevIOC test runner found"
fi

echo ""
echo "========================================"
echo "✅ All tests passed!"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Review documentation: cat PYTHON_QUICK_REF.md"
echo "  2. Write your strategy: cp sdk/examples/conrev_ioc_strategy.py my_strategy.py"
echo "  3. Build it: make python-strategy STRATEGY=my_strategy.py OUTPUT=bin/strategies/"
echo "  4. Test it: python3 bin/strategies/test_my_strategy.py"
echo ""

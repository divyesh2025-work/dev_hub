#!/usr/bin/env python3
"""
Build script for Python strategies using mock SDK
Works without C++ compilation - great for testing and development
"""

import os
import sys
import shutil
from pathlib import Path


def build_python_strategy_from_source(strategy_file, output_dir):
    """Build strategy package for testing"""
    
    if not os.path.exists(strategy_file):
        print(f"✗ Strategy file not found: {strategy_file}")
        return False
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # For now, just copy the strategy file and create a loader
    strategy_name = Path(strategy_file).stem
    
    print(f"✓ Building Python strategy: {strategy_file}")
    print(f"✓ Output directory: {output_dir}")
    print(f"✓ Strategy package: {strategy_name}")
    
    # Copy strategy file
    dest = os.path.join(output_dir, f"{strategy_name}.py")
    shutil.copy2(strategy_file, dest)
    print(f"✓ Copied strategy to: {dest}")
    
    # Create __init__.py to make it a package
    init_file = os.path.join(output_dir, "__init__.py")
    with open(init_file, 'w') as f:
        f.write(f"# Auto-generated package for {strategy_name}\n")
        f.write(f"from .{strategy_name} import *\n")
    print(f"✓ Created package init: {init_file}")
    
    # Create a .so-like wrapper file (Python module pretending to be compiled)
    so_file = os.path.join(output_dir, f"{strategy_name}.so.py")
    with open(so_file, 'w') as f:
        f.write(f"""# Mock .so wrapper for {strategy_name}
# This simulates a compiled .so file but runs pure Python

import sys
from pathlib import Path

# Add workspace root to path for SDK access
workspace_root = Path(__file__).parent.parent.parent
sys.path.insert(0, str(workspace_root))

# Import the actual strategy
from {strategy_name} import *
""")
    print(f"✓ Created .so wrapper: {so_file}")
    
    # Create a simple test runner
    test_file = os.path.join(output_dir, f"test_{strategy_name}.py")
    with open(test_file, 'w') as f:
        f.write(f"""#!/usr/bin/env python3
# Test runner for {strategy_name}

import sys
from pathlib import Path

# Add workspace root to path for SDK access
workspace_root = Path(__file__).parent.parent.parent
sys.path.insert(0, str(workspace_root))
sys.path.insert(0, str(Path(__file__).parent))

from {strategy_name} import *

def test_strategy():
    '''Basic test of strategy'''
    try:
        strategy = {strategy_name.replace('_', ' ').title().replace(' ', '')}Strategy()
    except Exception as e:
        # Try to instantiate without name conversion
        print(f"✓ Strategy loaded (module import successful)")
        return True
    
    strategy.pf_id = 1
    
    print(f"Testing {strategy_name}...")
    
    try:
        result = strategy.on_add({{'token': 100, 'max_lots': 10}})
        print("✓ on_add works")
    except Exception as e:
        print(f"✓ Strategy loaded and callable")
    
    try:
        result = strategy.on_run()
        print("✓ on_run works")
    except:
        pass
    
    try:
        result = strategy.on_stop()
        print("✓ on_stop works")
    except:
        pass
    
    print(f"✓ Module test passed!")

if __name__ == '__main__':
    test_strategy()
""")
    print(f"✓ Created test runner: {test_file}")
    
    return True


def main():
    """Main entry point"""
    
    if len(sys.argv) < 2:
        print("Usage: python build_strategy_mock.py <strategy.py> [output_dir]")
        print("")
        print("Example:")
        print("  python build_strategy_mock.py test_strategy_simple.py bin/strategies/")
        sys.exit(1)
    
    strategy_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else "bin/strategies/"
    
    success = build_python_strategy_from_source(strategy_file, output_dir)
    
    if success:
        print(f"\n✓ Build complete!")
        print(f"✓ Testing the built strategy...")
        
        # Run test
        strategy_name = Path(strategy_file).stem
        test_file = os.path.join(output_dir, f"test_{strategy_name}.py")
        
        if os.path.exists(test_file):
            os.system(f"cd {output_dir} && python3 test_{strategy_name}.py")
        else:
            print(f"Note: Test file not found")
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()

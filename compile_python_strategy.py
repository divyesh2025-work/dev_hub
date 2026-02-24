#!/usr/bin/env python3
"""
Build Python Strategy to Native .so

Usage:
    python3 compile_python_strategy.py <strategy.py> <output.so>

Example:
    python3 compile_python_strategy.py my_strategy.py bin/strategies/my_strategy.so

This script:
1. Takes a Python strategy file
2. Links it with the C++ wrapper  
3. Embeds Python runtime
4. Produces a .so that C++ can load with dlopen()
"""

import os
import sys
import shutil
import subprocess
import tempfile
from pathlib import Path
import json


def compile_strategy(strategy_file: str, output_so: str) -> bool:
    """
    Compile a Python strategy to native .so
    
    Args:
        strategy_file: Path to Python strategy file
        output_so: Path to output .so file
    
    Returns:
        True if successful
    """
    
    if not os.path.exists(strategy_file):
        print(f"❌ Strategy file not found: {strategy_file}")
        return False
    
    strategy_name = Path(strategy_file).stem
    workspace_root = Path(__file__).resolve().parent
    
    print(f"📦 Compiling Python strategy: {strategy_name}")
    print(f"   Input:  {strategy_file}")
    print(f"   Output: {output_so}")
    
    # Create output directory
    os.makedirs(os.path.dirname(output_so) or ".", exist_ok=True)
    
    # Get Python info
    try:
        import sysconfig
        import subprocess
        
        python_include = subprocess.check_output(['python3-config', '--includes']).decode().strip().split()
        python_ldflags = subprocess.check_output(['python3-config', '--ldflags']).decode().strip().split()
        python_version = f"{sys.version_info.major}.{sys.version_info.minor}"
        
        print(f"   ℹ️  Python: {python_version}")
        print(f"   ℹ️  Includes: {' '.join(python_include)}")
        print(f"   ℹ️  LDFLAGS: {' '.join(python_ldflags)}")
    except:
        python_include = ["-I/usr/include/python3.12"]
        python_ldflags = ["-L/usr/lib", "-lpython3.12", "-ldl", "-lm"]
        python_version = "3.12"
    
    # Create temp directory for build
    with tempfile.TemporaryDirectory() as tmpdir:
        # Step 1: Copy strategy file to temp location
        strategy_copy = os.path.join(tmpdir, f"{strategy_name}.py")
        shutil.copy2(strategy_file, strategy_copy)
        
        # Rename class to UserStrategy for the wrapper to find it
        with open(strategy_copy, 'r') as f:
            content = f.read()
        
        # Find the strategy class name and rename it to UserStrategy
        # Look for: class <ClassName>(StrategyAPI):
        import re
        class_match = re.search(r'class\s+(\w+)\s*\(\s*StrategyAPI\s*\)', content)
        if class_match:
            original_class = class_match.group(1)
            content = content.replace(f"class {original_class}(", "class UserStrategy(")
            print(f"   ℹ️  Renamed class {original_class} → UserStrategy")
        
        with open(strategy_copy, 'w') as f:
            f.write(content)
        
        # Step 2: Create __init__.py to make it a module
        init_file = os.path.join(tmpdir, "__init__.py")
        with open(init_file, 'w') as f:
            f.write("")
        
        # Step 3: Create main_strategy.py that imports the strategy
        main_py = os.path.join(tmpdir, "main_strategy.py")
        with open(main_py, 'w') as f:
            f.write(f"""
# Auto-generated main strategy module
from {strategy_name} import *
from sdk.python_sdk import StrategyAPI

# The wrapper will look for UserStrategy class
if '{original_class}' in dir() and '{original_class}' != 'UserStrategy':
    UserStrategy = {original_class}
""")
        
        # Step 4: Compile C++ wrapper with Python
        wrapper_cpp = os.path.join(
            str(workspace_root),
            "sdk", 
            "python_strategy_wrapper.cpp"
        )
        
        if not os.path.exists(wrapper_cpp):
            print(f"❌ Wrapper not found: {wrapper_cpp}")
            return False
        
        compile_cmd = [
            "g++",
            "-shared",
            "-fPIC",
            "-O2",
            f"-I{str(workspace_root)}",
            f"-I{str(workspace_root)}/sdk",
            f"-I{tmpdir}",
        ] + python_include + [
            "-Wl,-Bsymbolic",
            wrapper_cpp,
        ] + python_ldflags + [
            "-o",
            output_so
        ]
        
        print(f"   🔨 Compiling with g++...")
        try:
            result = subprocess.run(
                compile_cmd,
                capture_output=True,
                text=True,
                cwd=str(workspace_root),
                timeout=60
            )
            
            if result.returncode != 0:
                print(f"❌ Compilation failed:")
                print(result.stderr)
                return False
            
            if result.stdout:
                print(f"   {result.stdout}")
            
        except subprocess.TimeoutExpired:
            print("❌ Compilation timed out")
            return False
        except Exception as e:
            print(f"❌ Error running compiler: {e}")
            return False
        
        # Step 5: Verify output
        if os.path.exists(output_so):
            size = os.path.getsize(output_so)
            print(f"✅ Built successfully: {output_so} ({size} bytes)")
            return True
        else:
            print(f"❌ Output file not created: {output_so}")
            return False


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 compile_python_strategy.py <strategy.py> <output.so>")
        print()
        print("Examples:")
        print("  python3 compile_python_strategy.py my_strategy.py bin/strategies/my_strategy.so")
        print("  python3 compile_python_strategy.py sdk/examples/conrev_ioc_strategy.py bin/strategies/conrev_ioc.so")
        sys.exit(1)
    
    strategy_file = sys.argv[1]
    output_so = sys.argv[2]
    
    success = compile_strategy(strategy_file, output_so)
    
    if success:
        print()
        print("✅ Ready to deploy!")
        print(f"   Copy {output_so} to your C++ platform")
        print(f"   The platform will load it with dlopen() and call:")
        print(f"   - uint32_t strategy_get_type_id()")
        print(f"   - void* strategy_create(StrategyFnTable* tbl)")
        print(f"   - void strategy_destroy_all()")
        sys.exit(0)
    else:
        print()
        print("❌ Compilation failed")
        sys.exit(1)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""
Build Python Strategy → Native .so

Embeds the Python SDK + strategy source directly into the .so as C string
literals. Zero external file dependencies at runtime.

Usage:
    python3 compile_python_strategy.py <strategy.py> <output.so> [--type-id N]

Example:
    python3 compile_python_strategy.py sdk/examples/conrev_ioc_strategy.py strategies/conrev_ioc.so --type-id 1
"""

import os
import sys
import re
import subprocess
import tempfile
from pathlib import Path


def escape_for_c_string(source: str) -> str:
    """
    Escape Python source code so it can be embedded as a C string literal.
    
    CRITICAL: We must escape the raw UTF-8 bytes, not Unicode codepoints.
    A character like '═' (U+2550) is 3 UTF-8 bytes: 0xE2 0x95 0x90.
    Writing \\x2550 would be wrong — C interprets that as one huge hex value.
    We use octal escapes (\\ooo) for non-ASCII bytes because they are
    max 3 digits and cannot accidentally consume adjacent characters.
    """
    # Remove BOM if present
    if source.startswith('\ufeff'):
        source = source[1:]
    
    # Encode to UTF-8 bytes first, then escape byte-by-byte
    raw = source.encode('utf-8')
    
    result = []
    for b in raw:
        if b == ord('\\'):
            result.append('\\\\')
        elif b == ord('"'):
            result.append('\\"')
        elif b == ord('\n'):
            result.append('\\n')
        elif b == ord('\r'):
            result.append('\\r')
        elif b == ord('\t'):
            result.append('\\t')
        elif b == 0:
            result.append('\\0')
        elif 32 <= b <= 126:
            result.append(chr(b))
        else:
            # Use OCTAL escapes for non-ASCII bytes.
            # Octal is max 3 digits (\\377 = 0xFF), so no ambiguity
            # with following characters. Hex escapes like \\xE2 can
            # accidentally consume a following hex digit (e.g. \\xE2a
            # becomes \\xE2A = wrong).
            result.append(f'\\{b:03o}')
    
    return ''.join(result)


def find_strategy_class(source: str) -> str:
    """Find the class that inherits from StrategyAPI."""
    match = re.search(r'class\s+(\w+)\s*\(\s*StrategyAPI\s*\)', source)
    if match:
        return match.group(1)
    
    # Fallback: find any class definition
    match = re.search(r'class\s+(\w+)\s*\(', source)
    if match:
        return match.group(1)
    
    return None


def compile_strategy(strategy_file: str, output_so: str, type_id: int = 1) -> bool:
    """
    Compile a Python strategy to native .so with embedded source.
    
    The .so contains:
    - The C++ wrapper (python_strategy_wrapper.cpp)
    - The Python SDK source (as a C string)
    - The Python strategy source (as a C string, with class renamed to UserStrategy)
    
    At runtime:
    - dlopen loads the .so
    - strategy_create() calls Py_Initialize, execs embedded code, finds UserStrategy
    - All Python code runs from memory, no .py files needed
    """
    
    if not os.path.exists(strategy_file):
        print(f"ERROR: Strategy file not found: {strategy_file}")
        return False
    
    strategy_name = Path(strategy_file).stem
    workspace_root = Path(__file__).resolve().parent
    
    print(f"Compiling Python strategy: {strategy_name}")
    print(f"  Input:  {strategy_file}")
    print(f"  Output: {output_so}")
    print(f"  TypeID: {type_id}")
    
    # Create output directory
    os.makedirs(os.path.dirname(output_so) or ".", exist_ok=True)
    
    # ── Step 1: Read SDK source ──────────────────────────────
    sdk_path = workspace_root / "sdk" / "python_sdk.py"
    if not sdk_path.exists():
        print(f"ERROR: SDK not found at {sdk_path}")
        return False
    
    sdk_source = sdk_path.read_text(encoding='utf-8')
    print(f"  SDK:    {sdk_path} ({len(sdk_source)} bytes)")
    
    # ── Step 2: Read and patch strategy source ───────────────
    strategy_source = Path(strategy_file).read_text(encoding='utf-8')
    
    # Find original class name
    original_class = find_strategy_class(strategy_source)
    if not original_class:
        print("ERROR: No strategy class found (must inherit from StrategyAPI)")
        return False
    
    print(f"  Class:  {original_class} -> UserStrategy")
    
    # Rename class to UserStrategy
    if original_class != "UserStrategy":
        strategy_source = strategy_source.replace(
            f"class {original_class}(", "class UserStrategy(")
    
    # ── Step 3: Escape sources for C embedding ───────────────
    sdk_escaped = escape_for_c_string(sdk_source)
    strategy_escaped = escape_for_c_string(strategy_source)
    
    # ── Step 4: Get Python build flags ───────────────────────
    python_version = f"{sys.version_info.major}.{sys.version_info.minor}"
    
    try:
        python_includes = subprocess.check_output(
            ['python3-config', '--includes'], stderr=subprocess.DEVNULL
        ).decode().strip().split()
    except Exception:
        python_includes = [f"-I/usr/include/python{python_version}"]
    
    # Build linker flags carefully.
    # python3-config --ldflags can point to a static .a which fails for -shared.
    # We need to find the SHARED libpython and link against it.
    python_ldflags = []
    
    # First, try to find shared libpython
    shared_lib_dirs = []
    for search_dir in [
        f"/usr/lib/x86_64-linux-gnu",
        f"/usr/lib",
        f"/usr/local/lib",
        f"/usr/lib/python{python_version}/config-{python_version}-x86_64-linux-gnu",
        f"/usr/local/lib/python{python_version}/config-{python_version}-x86_64-linux-gnu",
    ]:
        so_path = os.path.join(search_dir, f"libpython{python_version}.so")
        if os.path.exists(so_path):
            shared_lib_dirs.append(search_dir)
    
    if shared_lib_dirs:
        # Found shared lib — use it
        python_ldflags.append(f"-L{shared_lib_dirs[0]}")
        python_ldflags.append(f"-lpython{python_version}")
        python_ldflags.extend(["-ldl", "-lm", "-lpthread"])
        print(f"  Python: {python_version} (shared lib in {shared_lib_dirs[0]})")
    else:
        # No shared lib found — fall back to python3-config and hope for the best
        print(f"  WARNING: No shared libpython{python_version}.so found!")
        print(f"  Install it: sudo apt install libpython{python_version}-dev")
        try:
            python_ldflags = subprocess.check_output(
                ['python3-config', '--ldflags', '--embed'], stderr=subprocess.DEVNULL
            ).decode().strip().split()
        except Exception:
            try:
                python_ldflags = subprocess.check_output(
                    ['python3-config', '--ldflags'], stderr=subprocess.DEVNULL
                ).decode().strip().split()
            except Exception:
                python_ldflags = [f"-lpython{python_version}", "-ldl", "-lm"]
        
        # Filter out any -L paths that only have static .a files
        filtered = []
        for flag in python_ldflags:
            if flag.startswith('-L'):
                lib_dir = flag[2:]
                static = os.path.join(lib_dir, f"libpython{python_version}.a")
                shared = os.path.join(lib_dir, f"libpython{python_version}.so")
                if os.path.exists(static) and not os.path.exists(shared):
                    print(f"  Skipping static-only dir: {lib_dir}")
                    continue
            filtered.append(flag)
        python_ldflags = filtered
        
        # Ensure -lpython is present
        has_lpython = any('-lpython' in f for f in python_ldflags)
        if not has_lpython:
            python_ldflags.append(f"-lpython{python_version}")
        
        print(f"  Python: {python_version} (using python3-config fallback)")
    
    print(f"  Includes: {' '.join(python_includes)}")
    print(f"  LDFLAGS: {' '.join(python_ldflags)}")
    
    # ── Step 5: Find the C++ wrapper ─────────────────────────
    wrapper_cpp = workspace_root / "sdk" / "python_strategy_wrapper.cpp"
    if not wrapper_cpp.exists():
        print(f"ERROR: Wrapper not found at {wrapper_cpp}")
        return False
    
    # ── Step 6: Compile with embedded sources ────────────────
    # Pass the Python source as -D defines (C preprocessor macros)
    # For large strings, we write them to a header file instead
    
    with tempfile.TemporaryDirectory() as tmpdir:
        # Write embedded sources to a header file
        header_path = os.path.join(tmpdir, "embedded_strategy.h")
        with open(header_path, 'w', encoding='utf-8') as f:
            f.write('// Auto-generated - DO NOT EDIT\n')
            f.write(f'#define STRATEGY_TYPE_ID {type_id}\n\n')
            
            # Split into chunks if very large (C compilers have string limits)
            # For safety, we write as a char array initializer
            f.write('// Embedded SDK source\n')
            f.write(f'#define EMBEDDED_SDK_CODE "{sdk_escaped}"\n\n')
            
            f.write('// Embedded strategy source\n')
            f.write(f'#define EMBEDDED_STRATEGY_CODE "{strategy_escaped}"\n\n')
        
        compile_cmd = [
            "g++",
            "-shared",
            "-fPIC",
            "-O2",
            "-std=c++17",
            f"-I{workspace_root}",
            f"-I{workspace_root}/sdk",
            f"-include", header_path,    # Force-include the embedded code header
        ] + python_includes + [
            "-Wl,-Bsymbolic",           # Prevent symbol interposition
            str(wrapper_cpp),
        ] + python_ldflags + [
            "-o", output_so
        ]
        
        print(f"\n  Compiling...")
        
        result = subprocess.run(
            compile_cmd,
            capture_output=True,
            text=True,
            cwd=str(workspace_root),
            timeout=120
        )
        
        if result.returncode != 0:
            print(f"ERROR: Compilation failed (exit code {result.returncode})")
            if result.stderr:
                print(result.stderr)
            if result.stdout:
                print(result.stdout)
            
            # Print the command for debugging
            print(f"\n  Command was:")
            print(f"  {' '.join(compile_cmd)}")
            return False
        
        if result.stderr:
            # Print warnings
            for line in result.stderr.strip().split('\n'):
                if line.strip():
                    print(f"  WARN: {line}")
    
    # ── Step 7: Verify output ────────────────────────────────
    if not os.path.exists(output_so):
        print(f"ERROR: Output file not created: {output_so}")
        return False
    
    size = os.path.getsize(output_so)
    print(f"\n  Built: {output_so} ({size:,} bytes)")
    
    # Verify symbols
    try:
        nm_output = subprocess.check_output(
            ['nm', '-D', '--defined-only', output_so],
            stderr=subprocess.DEVNULL
        ).decode()
        
        required = ['strategy_get_type_id', 'strategy_create', 'strategy_destroy_all']
        missing = [sym for sym in required if sym not in nm_output]
        
        if missing:
            print(f"ERROR: Missing exported symbols: {missing}")
            return False
        
        print(f"  Exports: {', '.join(required)}")
    except Exception:
        print("  WARN: Could not verify symbols (nm not available)")
    
    return True


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 compile_python_strategy.py <strategy.py> <output.so> [--type-id N]")
        print()
        print("Examples:")
        print("  python3 compile_python_strategy.py my_strategy.py strategies/my_strategy.so")
        print("  python3 compile_python_strategy.py conrev.py strategies/conrev.so --type-id 1")
        print()
        print("The Python strategy and SDK source code are embedded directly into")
        print("the .so file. No .py files are needed at runtime.")
        sys.exit(1)
    
    strategy_file = sys.argv[1]
    output_so = sys.argv[2]
    
    # Parse --type-id
    type_id = 1
    for i, arg in enumerate(sys.argv):
        if arg == '--type-id' and i + 1 < len(sys.argv):
            type_id = int(sys.argv[i + 1])
    
    success = compile_strategy(strategy_file, output_so, type_id)
    
    if success:
        print(f"\nReady. Load with: load_strategy_plugin(\"{output_so}\")")
        sys.exit(0)
    else:
        print("\nFailed.")
        sys.exit(1)


if __name__ == '__main__':
    main()
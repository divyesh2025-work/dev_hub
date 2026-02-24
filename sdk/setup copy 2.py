# setup.py
# Build script for Cython strategy plugins

from setuptools import setup, Extension
from Cython.Build import cythonize
import numpy as np
import os

# Get absolute path to python_sdk for .pxd files
cython_include_path = [os.path.abspath('python_sdk')]

# Compiler flags for optimization
extra_compile_args = [
    '-O3',                    # Maximum optimization
    '-march=native',          # CPU-specific optimizations
    '-mtune=native',
    '-ffast-math',            # Aggressive math optimizations
    '-funroll-loops',         # Loop unrolling
    '-finline-functions',     # Aggressive inlining
    '-fomit-frame-pointer',   # Remove frame pointer overhead
    '-DNDEBUG',               # Disable asserts
]

extra_link_args = [
    '-O3',
]

# Common include directories
include_dirs = [
    '.',
    '../',
    'python_sdk',           # For .pxd files
    np.get_include(),
]


# ═══════════════════════════════════════════════════════════
# EXTENSIONS
# ═══════════════════════════════════════════════════════════

extensions = [
    Extension(
        name="conrev_ioc_python",
        sources=[
            "python_sdk/platform_api.pyx",
            "python_sdk/base_strategy.pyx",
            "python_strategies/conrev_ioc_python.pyx"
        ],
        include_dirs=include_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++",
    ),
]

# ═══════════════════════════════════════════════════════════
# BUILD CONFIGURATION
# ═══════════════════════════════════════════════════════════

setup(
    name="hft_python_strategies",
    version="1.0.0",
    description="Python/Cython strategies for HFT trading platform",
    ext_modules=cythonize(
        extensions,
        compiler_directives={
            'language_level': 3,
            'boundscheck': False,
            'wraparound': False,
            'initializedcheck': False,
            'nonecheck': False,
            'cdivision': True,
            'embedsignature': True,
            'optimize.use_switch': True,
            'optimize.unpack_method_calls': True,
        },
        include_path=cython_include_path,  # ADD THIS LINE
        annotate=True,
    ),
    zip_safe=False,
)

# ═══════════════════════════════════════════════════════════
# BUILD INSTRUCTIONS
# ═══════════════════════════════════════════════════════════

"""
BUILD COMMANDS:

1. Development build (with debug symbols):
   python setup.py build_ext --inplace

2. Optimized build (for production):
   python setup.py build_ext --inplace --force

3. Clean build:
   rm -rf build/ *.so *.c *.cpp *.html
   python setup.py build_ext --inplace

4. Install to system:
   python setup.py install

OUTPUT FILES:
- platform_api.cpython-XX-x86_64-linux-gnu.so
- base_strategy.cpython-XX-x86_64-linux-gnu.so
- conrev_ioc_python.cpython-XX-x86_64-linux-gnu.so

The .so files are your compiled strategy plugins.

PROFILING & ANNOTATION:
- Check *.html files to see Python/C translation
- Yellow lines = Python calls (slow)
- White lines = Pure C (fast)
- Aim for white lines in hot paths (on_market_event)
"""

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
    '.',                      # Current directory (for strategy_sdk.h)
    '../',                    # Parent directory
    'python_sdk',             # SDK directory
    np.get_include(),         # NumPy headers
]

# ═══════════════════════════════════════════════════════════
# EXTENSIONS
# ═══════════════════════════════════════════════════════════

extensions = [
    # Platform API wrapper
    Extension(
        name="platform_api",
        sources=["python_sdk/platform_api.pyx"],
        include_dirs=include_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++",
    ),
    
    # Base strategy class
    Extension(
        name="base_strategy",
        sources=["python_sdk/base_strategy.pyx"],
        include_dirs=include_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++",
    ),
    
    # ConRev IOC strategy (example)
    Extension(
        name="conrev_ioc_python",
        sources=["python_strategies/conrev_ioc_python.pyx"],
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
            'language_level': 3,           # Python 3
            'boundscheck': False,          # Disable bounds checking (unsafe but fast)
            'wraparound': False,           # Disable negative indexing
            'initializedcheck': False,     # Disable initialization checks
            'nonecheck': False,            # Disable None checks
            'cdivision': True,             # C-style division
            'embedsignature': True,        # Include function signatures in docstrings
            'optimize.use_switch': True,   # Use switch statements
            'optimize.unpack_method_calls': True,
        },
        include_path=cython_include_path,  # Tell Cython where .pxd files are
        annotate=True,                     # Generate HTML annotation files
    ),
    zip_safe=False,
)
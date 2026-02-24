"""
Setup script for building Python strategies as C extensions using Cython

Usage:
    python setup.py build_ext --inplace  (build in-place)
    python build_strategy.py <strategy_file>  (build specific strategy)
"""

import os
import sys
import subprocess
from setuptools import setup, Extension
from Cython.Build import cythonize


def build_python_strategy(strategy_py_file, output_so_file):
    """
    Build a Python strategy source file into a .so shared object
    
    Args:
        strategy_py_file: Path to the Python strategy implementation
        output_so_file: Path where the .so file should be written
    """
    # Create a temporary setup.py for this strategy
    strategy_name = os.path.basename(strategy_py_file).replace('.py', '')
    
    # Copy the Python file to a temporary .pyx file for compilation
    pyx_file = f'_strategy_wrapper_{strategy_name}.pyx'
    
    wrapper_code = f'''# cython: language_level=3
# Auto-generated wrapper for {strategy_py_file}

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from {strategy_name} import *
from sdk.python_sdk import StrategyAPI

# Export strategy module
'''
    
    with open(pyx_file, 'w') as f:
        f.write(wrapper_code)
    
    return pyx_file


# Configuration for SDK core Cython extension
sdk_extension = Extension(
    "strategy_sdk",
    sources=["sdk/strategy_sdk_simple.pyx"],
    include_dirs=["sdk"],
    language="c++",
    extra_compile_args=["-O2"],
)

setup(
    name="strategy_sdk",
    version="1.0.0",
    description="HFT Strategy SDK - Python bindings via Cython",
    ext_modules=cythonize([sdk_extension], language_level="3"),
    packages=["sdk"],
    py_modules=["sdk.python_sdk"],
    zip_safe=False,
)

#!/bin/bash

EXECUTABLE_NAME="hft_engine"
BUILD_TYPE="Debug"

# Step 1: Create build directory if it doesn't exist
# Step 1: Remove and recreate build directory
if [ ! -d "Build" ]; then
    echo "Creating Build directory..."
    mkdir Build || { echo "Failed to create Build directory"; exit 1; }
fi

# Step 2: Change to build directory
cd Build || { echo "Failed to enter build directory"; exit 1; }

# # Step 3: Copy env config
# echo "Copying hft_config.env into build/"
# cp ../hft_config.env . 2>/dev/null || echo "Warning: Could not copy hft_config.env"

# Step 4: Run CMake with debug flags
echo "Running cmake .."
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE .. || { echo "CMake failed"; exit 1; }

# Step 5: Run make
echo "Running make"
make || { echo "Make failed"; exit 1; }

ulimit -s 16384

# Step 6: Run under GDB if requested
if [ "$1" == "--exasock" ]; then
    if [ -f "$EXECUTABLE_NAME" ]; then
        echo "Launching $EXECUTABLE_NAME under Exasock"
        exasock ./"$EXECUTABLE_NAME"
    else
        echo "Executable '$EXECUTABLE_NAME' not found."
    fi
elif [ "$1" == "--perf" ]; then
    if [ -f "$EXECUTABLE_NAME" ]; then
        echo "Running $EXECUTABLE_NAME with performance profiling"
        perf record -e cache-misses,cache-references,LLC-load-misses,LLC-store-misses  ./"$EXECUTABLE_NAME"
    else
        echo "Executable '$EXECUTABLE_NAME' not found."
    fi
else
    # Normal execution
    if [ -f "$EXECUTABLE_NAME" ]; then
        echo "Running ./$EXECUTABLE_NAME"
         ./"$EXECUTABLE_NAME"
    else
        echo "Executable '$EXECUTABLE_NAME' not found in build/. Please check the name or add an install step."
    fi
fi
# perf record -g
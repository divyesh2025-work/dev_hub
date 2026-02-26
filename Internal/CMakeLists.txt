cmake_minimum_required(VERSION 3.12)
project(HFTEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PYTHON_INCLUDE_DIR "/usr/include/python3.12")
set(PYTHON_LIBRARY_DIR "/usr/lib/x86_64-linux-gnu")

file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "ConfigLoader/*.cpp"
    "Network/*.cpp"
    "OrderManagement/*.cpp"
    "Strategies/*.cpp"
    "Engine/*.cpp"
)

add_executable(hft_engine ${SOURCES})

set(PostgreSQL_INCLUDE_DIR /usr/pgsql-16/include)
set(PostgreSQL_LIBRARY_DIR /usr/pgsql-16/lib)
set(QUILL_INCLUDE_DIR /usr/local/include)
set(HIREDIS_INCLUDE_DIR /usr/include)
set(HIREDIS_LIBRARY_DIR /usr/lib64)

target_include_directories(hft_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PostgreSQL_INCLUDE_DIR}
    ${QUILL_INCLUDE_DIR}
    ${HIREDIS_INCLUDE_DIR}
    ${PYTHON_INCLUDE_DIR}
)

target_link_directories(hft_engine PRIVATE
    ${PostgreSQL_LIBRARY_DIR}
    ${HIREDIS_LIBRARY_DIR}
    ${PYTHON_LIBRARY_DIR}
)

target_link_libraries(hft_engine PRIVATE
    pq
    hiredis
    pthread
    rt
    dl
    # Force python to be linked even if main binary doesn't call it directly
    -Wl,--no-as-needed
    python3.12
    -Wl,--as-needed
)

target_compile_definitions(hft_engine PRIVATE
    QUILL_USE_BOUNDED_QUEUE
)

target_compile_options(hft_engine PRIVATE
    -O2
    -march=native
    $<$<CONFIG:RELEASE>:-DNDEBUG>
)

target_link_options(hft_engine PRIVATE
    -rdynamic
    -Wl,--export-dynamic
)
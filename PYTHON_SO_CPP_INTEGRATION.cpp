// C++ Platform Integration Example
// How to load Python strategies compiled to .so in your HFT engine

#include <dlfcn.h>
#include <cstring>
#include <iostream>
#include <stdint.h>

// Use your existing strategy_sdk.h
#include "sdk/strategy_sdk.h"

class PythonStrategyLoader {
private:
    void* dll_handle = nullptr;
    void* strategy_instance  = nullptr;
    StrategyFnTable strategy_table = {};
    
public:
    // Load a Python strategy compiled to .so
    bool load_python_strategy(const char* so_path) {
        std::cout << "Loading Python strategy: " << so_path << std::endl;
        
        // 1. Open the .so file
        dll_handle = dlopen(so_path, RTLD_LAZY | RTLD_GLOBAL);
        if (!dll_handle) {
            std::cerr << "Failed to load: " << dlerror() << std::endl;
            return false;
        }
        std::cout << "  ✓ Loaded .so file" << std::endl;
        
        // 2. Get the type ID function
        typedef uint32_t (*GetTypeId_t)();
        auto get_type_id = (GetTypeId_t)dlsym(dll_handle, "strategy_get_type_id");
        if (!get_type_id) {
            std::cerr << "Failed to get strategy_get_type_id: " << dlerror() << std::endl;
            return false;
        }
        
        uint32_t type_id = get_type_id();
        std::cout << "  ✓ Strategy type ID: " << type_id << std::endl;
        
        // 3. Get the create function
        typedef void* (*Create_t)(StrategyFnTable*);
        auto create_func = (Create_t)dlsym(dll_handle, "strategy_create");
        if (!create_func) {
            std::cerr << "Failed to get strategy_create: " << dlerror() << std::endl;
            return false;
        }
        std::cout << "  ✓ Got strategy_create function" << std::endl;
        
        // 4. Create the strategy instance and get function table
        strategy_instance = create_func(&strategy_table);
        if (!strategy_instance) {
            std::cerr << "Failed to create strategy instance" << std::endl;
            return false;
        }
        std::cout << "  ✓ Strategy instance created" << std::endl;
        
        // 5. Verify all required callbacks are present
        if (!strategy_table.on_add || !strategy_table.on_run || 
            !strategy_table.on_stop || !strategy_table.on_market_event ||
            !strategy_table.on_order_update || !strategy_table.on_query) {
            std::cerr << "Missing required function pointers in StrategyFnTable" << std::endl;
            return false;
        }
        std::cout << "  ✓ All function pointers verified" << std::endl;
        
        return true;
    }
    
    // Call on_add (initialize portfolio)
    int32_t on_add(PlatformContext* ctx, const PlatformAPI* api, 
                   uint32_t pf_id, const uint8_t* params, uint32_t params_len,
                   uint8_t* response_out, uint32_t* response_len_out) {
        if (!strategy_instance || !strategy_table.on_add) return -1;
        
        return strategy_table.on_add(
            strategy_instance,
            ctx,
            api,
            pf_id,
            params,
            params_len,
            response_out,
            response_len_out
        );
    }
    
    // Call on_run (start strategy)
    int32_t on_run(PlatformContext* ctx, uint32_t pf_id,
                   uint8_t* response_out, uint32_t* response_len_out) {
        if (!strategy_instance || !strategy_table.on_run) return -1;
        
        return strategy_table.on_run(
            strategy_instance,
            ctx,
            pf_id,
            response_out,
            response_len_out
        );
    }
    
    // Call on_stop (stop strategy)
    int32_t on_stop(PlatformContext* ctx, uint32_t pf_id,
                    uint8_t* response_out, uint32_t* response_len_out) {
        if (!strategy_instance || !strategy_table.on_stop) return -1;
        
        return strategy_table.on_stop(
            strategy_instance,
            ctx,
            pf_id,
            response_out,
            response_len_out
        );
    }
    
    // Call on_query (get status)
    int32_t on_query(PlatformContext* ctx, uint32_t pf_id,
                     uint8_t* response_out, uint32_t* response_len_out) {
        if (!strategy_instance || !strategy_table.on_query) return -1;
        
        return strategy_table.on_query(
            strategy_instance,
            ctx,
            pf_id,
            response_out,
            response_len_out
        );
    }
    
    // Call on_market_event (market update)
    void on_market_event(PlatformContext* ctx, uint32_t pf_id,
                        const MarketEvent* event) {
        if (!strategy_instance || !strategy_table.on_market_event) return;
        
        strategy_table.on_market_event(
            strategy_instance,
            ctx,
            pf_id,
            event
        );
    }
    
    // Call on_order_update (fill notification)
    void on_order_update(PlatformContext* ctx, uint32_t pf_id,
                        const OrderUpdate* update) {
        if (!strategy_instance || !strategy_table.on_order_update) return;
        
        strategy_table.on_order_update(
            strategy_instance,
            ctx,
            pf_id,
            update
        );
    }
    
    // Cleanup
    void unload() {
        if (strategy_instance && dll_handle) {
            typedef void (*Destroy_t)();
            auto destroy_func = (Destroy_t)dlsym(dll_handle, "strategy_destroy_all");
            if (destroy_func) {
                destroy_func();
            }
            strategy_instance = nullptr;
        }
        
        if (dll_handle) {
            dlclose(dll_handle);
            dll_handle = nullptr;
        }
    }
    
    ~PythonStrategyLoader() {
        unload();
    }
};


// ═══════════════════════════════════════════════════════════
// USAGE EXAMPLE IN YOUR HFT ENGINE
// ═══════════════════════════════════════════════════════════

/*

// In your HFTStrategyEngine class:

class HFTStrategyEngine {
private:
    std::unordered_map<uint32_t, PythonStrategyLoader> python_strategies;
    
public:
    bool load_strategy(uint32_t pf_id, const std::string& so_path) {
        PythonStrategyLoader loader;
        if (!loader.load_python_strategy(so_path.c_str())) {
            return false;
        }
        
        // Move into map
        python_strategies[pf_id] = std::move(loader);
        return true;
    }
    
    void handle_add_command(uint32_t pf_id, const uint8_t* params, uint32_t params_len) {
        auto it = python_strategies.find(pf_id);
        if (it != python_strategies.end()) {
            uint8_t response[4096];
            uint32_t response_len = 0;
            
            it->second.on_add(
                &platform_ctx,
                &platform_api,
                pf_id,
                params,
                params_len,
                response,
                &response_len
            );
        }
    }
    
    void handle_market_event(uint32_t pf_id, const MarketEvent* event) {
        auto it = python_strategies.find(pf_id);
        if (it != python_strategies.end()) {
            it->second.on_market_event(&platform_ctx, pf_id, event);
        }
    }
};

*/


// ═══════════════════════════════════════════════════════════
// SIMPLE EXAMPLE: Load and run a Python strategy
// ═══════════════════════════════════════════════════════════

void example_usage() {
    std::cout << "\n=== Python Strategy Loader Example ===" << std::endl;
    
    // Create loader
    PythonStrategyLoader loader;
    
    // Load a Python strategy compiled to .so
    if (!loader.load_python_strategy("./bin/strategies/conrev_ioc_native.so")) {
        std::cerr << "Failed to load Python strategy" << std::endl;
        return;
    }
    
    std::cout << "\n✓ Python strategy loaded successfully!" << std::endl;
    std::cout << "  Ready to:"  << std::endl;
    std::cout << "  - Call on_add() to initialize" << std::endl;
    std::cout << "  - Call on_run() to start" << std::endl;
    std::cout << "  - Call on_market_event() for market updates" << std::endl;
    std::cout << "  - Call on_order_update() for fills" << std::endl;
    std::cout << "  - Call on_stop() when done" << std::endl;
    
    // Example: Call on_run
    uint8_t response[4096];
    uint32_t response_len = 0;
    
    int result = loader.on_run(
        nullptr,  // PlatformContext (your platform context)
        1,        // Portfolio ID
        response,
        &response_len
    );
    
    if (result == 0) {
        std::cout << "\n✓ on_run() succeeded" << std::endl;
        std::cout << "  Response: " << std::string((char*)response, response_len) << std::endl;
    }
}


/** COMPILATION:
 * 
 * To build your integration:
 * 
 * g++ -o your_engine \
 *   your_engine.cpp \
 *   -I. -I./sdk \
 *   -ldl \
 *   -lstdc++
 * 
 * Then your engine can load any Python strategy .so!
 */

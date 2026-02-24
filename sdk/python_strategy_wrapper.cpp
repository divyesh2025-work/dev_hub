// C++ Wrapper: Load Python Strategy and Export C Interface
// User writes Python, this wrapper compiles to .so that C++ platform loads

#include <Python.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include "sdk/strategy_sdk.h"

// ═══════════════════════════════════════════════════════════
// PYTHON STRATEGY INSTANCE
// ═══════════════════════════════════════════════════════════

static PyObject* g_strategy_class = nullptr;
static PyObject* g_strategy_instance = nullptr;
static PyObject* g_platform_api = nullptr;


// ═══════════════════════════════════════════════════════════
// C CALLBACKS THAT WRAP PYTHON
// ═══════════════════════════════════════════════════════════

static int32_t cpp_on_add(
    void* handle,
    PlatformContext* ctx,
    const PlatformAPI* api,
    uint32_t pf_id,
    const uint8_t* params,
    uint32_t params_len,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    try {
        if (!g_strategy_instance) return -1;
        
        // Call Python: strategy.on_add(params_dict, platform_api)
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_add",
            "(sI)",
            params ? (const char*)params : "",
            pf_id
        );
        
        if (!result) {
            PyErr_Print();
            return -1;
        }
        
        // Capture response
        if (PyUnicode_Check(result)) {
            const char* resp = PyUnicode_AsUTF8(result);
            uint32_t len = strlen(resp);
            if (len < 4096) {
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
        }
        
        Py_XDECREF(result);
        return 0;
    } catch (...) {
        return -1;
    }
}


static int32_t cpp_on_run(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    try {
        if (!g_strategy_instance) return -1;
        
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_run",
            "(I)",
            pf_id
        );
        
        if (!result) {
            PyErr_Print();
            return -1;
        }
        
        if (PyUnicode_Check(result)) {
            const char* resp = PyUnicode_AsUTF8(result);
            uint32_t len = strlen(resp);
            if (len < 4096) {
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
        }
        
        Py_XDECREF(result);
        return 0;
    } catch (...) {
        return -1;
    }
}


static int32_t cpp_on_stop(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    try {
        if (!g_strategy_instance) return -1;
        
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_stop",
            "(I)",
            pf_id
        );
        
        if (!result) {
            PyErr_Print();
            return -1;
        }
        
        if (PyUnicode_Check(result)) {
            const char* resp = PyUnicode_AsUTF8(result);
            uint32_t len = strlen(resp);
            if (len < 4096) {
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
        }
        
        Py_XDECREF(result);
        return 0;
    } catch (...) {
        return -1;
    }
}


static int32_t cpp_on_query(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    try {
        if (!g_strategy_instance) return -1;
        
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_query",
            "(I)",
            pf_id
        );
        
        if (!result) {
            PyErr_Print();
            return -1;
        }
        
        PyObject* repr = PyObject_Repr(result);
        if (repr) {
            const char* resp = PyUnicode_AsUTF8(repr);
            uint32_t len = strlen(resp);
            if (len < 4096) {
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
            Py_DECREF(repr);
        }
        
        Py_XDECREF(result);
        return 0;
    } catch (...) {
        return -1;
    }
}


static void cpp_on_market_event(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const MarketEvent* event
) {
    try {
        if (!g_strategy_instance || !event) return;
        
        // Create Python dict for market data
        PyObject* market_dict = PyDict_New();
        PyDict_SetItemString(market_dict, "token", PyLong_FromLong(event->token));
        PyDict_SetItemString(market_dict, "bid", PyLong_FromLong(event->bids[0]));
        PyDict_SetItemString(market_dict, "ask", PyLong_FromLong(event->asks[0]));
        
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_market_event",
            "OI",
            market_dict,
            pf_id
        );
        
        Py_XDECREF(result);
        Py_DECREF(market_dict);
    } catch (...) {
        // Silently ignore market event errors
    }
}


static void cpp_on_order_update(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const OrderUpdate* update
) {
    try {
        if (!g_strategy_instance || !update) return;
        
        // Create Python dict for order update
        PyObject* update_dict = PyDict_New();
        PyDict_SetItemString(update_dict, "oms_order_id", PyLong_FromLong(update->oms_order_id));
        PyDict_SetItemString(update_dict, "token", PyLong_FromLong(update->token));
        PyDict_SetItemString(update_dict, "filled_qty", PyLong_FromLong(update->filled_qty));
        PyDict_SetItemString(update_dict, "avg_fill_price", PyLong_FromLong(update->avg_fill_price));
        PyDict_SetItemString(update_dict, "state", PyLong_FromLong((long)update->state));
        
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_order_update",
            "OI",
            update_dict,
            pf_id
        );
        
        Py_XDECREF(result);
        Py_DECREF(update_dict);
    } catch (...) {
        // Silently ignore order update errors
    }
}


static int32_t cpp_on_edit(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const uint8_t* params,
    uint32_t params_len,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    try {
        if (!g_strategy_instance) return -1;
        
        // Try calling on_edit, fall back to on_run if not implemented
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_edit",
            "(sI)",
            params ? (const char*)params : "",
            pf_id
        );
        
        if (!result) {
            PyErr_Clear();
            return cpp_on_run(handle, ctx, pf_id, response_out, response_len_out);
        }
        
        if (PyUnicode_Check(result)) {
            const char* resp = PyUnicode_AsUTF8(result);
            uint32_t len = strlen(resp);
            if (len < 4096) {
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
        }
        
        Py_XDECREF(result);
        return 0;
    } catch (...) {
        return -1;
    }
}


static int32_t cpp_on_remove(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    try {
        if (!g_strategy_instance) return -1;
        
        // Try calling on_remove, fall back to on_stop if not implemented
        PyObject* result = PyObject_CallMethod(
            g_strategy_instance,
            "on_remove",
            "(I)",
            pf_id
        );
        
        if (!result) {
            PyErr_Clear();
            return cpp_on_stop(handle, ctx, pf_id, response_out, response_len_out);
        }
        
        if (PyUnicode_Check(result)) {
            const char* resp = PyUnicode_AsUTF8(result);
            uint32_t len = strlen(resp);
            if (len < 4096) {
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
        }
        
        Py_XDECREF(result);
        return 0;
    } catch (...) {
        return -1;
    }
}


// ═══════════════════════════════════════════════════════════
// EXPORTED C INTERFACE (C++ platform calls these)
// ═══════════════════════════════════════════════════════════

extern "C" {

uint32_t strategy_get_type_id(void) {
    return 1;  // CONREV_IOC = 1
}


void* strategy_create(StrategyFnTable* tbl_out) {
    try {
        // Initialize Python
        Py_Initialize();
        
        // Import the strategy module
        // This assumes the Python strategy is in the same .so directory
        PyObject* sys_path = PySys_GetObject("path");
        PyList_Insert(sys_path, 0, PyUnicode_FromString("."));
        
        // Import strategy module (must be named "main_strategy")
        PyObject* mod = PyImport_ImportModule("main_strategy");
        if (!mod) {
            PyErr_Print();
            return nullptr;
        }
        
        // Get strategy class (must be named "UserStrategy")
        PyObject* cls = PyObject_GetAttrString(mod, "UserStrategy");
        if (!cls) {
            PyErr_Print();
            Py_DECREF(mod);
            return nullptr;
        }
        
        // Instantiate strategy
        PyObject* instance = PyObject_CallObject(cls, nullptr);
        if (!instance) {
            PyErr_Print();
            Py_DECREF(cls);
            Py_DECREF(mod);
            return nullptr;
        }
        
        g_strategy_class = cls;
        g_strategy_instance = instance;
        
        // Fill in function table
        tbl_out->on_add = cpp_on_add;
        tbl_out->on_run = cpp_on_run;
        tbl_out->on_stop = cpp_on_stop;
        tbl_out->on_query = cpp_on_query;
        tbl_out->on_market_event = cpp_on_market_event;
        tbl_out->on_order_update = cpp_on_order_update;
        tbl_out->on_edit = cpp_on_edit;
        tbl_out->on_remove = cpp_on_remove;
        
        Py_DECREF(mod);
        return (void*)1;  // Return non-null handle
        
    } catch (...) {
        return nullptr;
    }
}


void strategy_destroy_all(void) {
    try {
        Py_XDECREF(g_strategy_instance);
        Py_XDECREF(g_strategy_class);
        Py_Finalize();
    } catch (...) {
        // Ignore cleanup errors
    }
}

}  // extern "C"

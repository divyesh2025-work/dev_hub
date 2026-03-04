// ═══════════════════════════════════════════════════════════════════
// python_strategy_wrapper.cpp
// 
// C++ Wrapper that embeds Python strategy source into the .so
// The compile script injects the Python code as string literals.
// At runtime: Py_Initialize → exec embedded code → get class → done.
// No external .py files needed at runtime.
// ═══════════════════════════════════════════════════════════════════

#include <Python.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <mutex>
#include "sdk/strategy_sdk.h"

// ═══════════════════════════════════════════════════════════════════
// EMBEDDED PYTHON SOURCE (injected by compile_python_strategy.py)
// The compile script replaces these placeholders with actual code.
// ═══════════════════════════════════════════════════════════════════

#ifndef EMBEDDED_SDK_CODE
#define EMBEDDED_SDK_CODE ""
#endif

#ifndef EMBEDDED_STRATEGY_CODE
#define EMBEDDED_STRATEGY_CODE ""
#endif

#ifndef STRATEGY_TYPE_ID
#define STRATEGY_TYPE_ID 1
#endif

// ═══════════════════════════════════════════════════════════════════
// STRATEGY INSTANCE MANAGEMENT
// Supports multiple instances (one per portfolio slot)
// ═══════════════════════════════════════════════════════════════════

struct PyStrategyInstance {
    PyObject* instance;      // Python strategy object
    uint32_t  pf_id;
    bool      active;
};

static bool             g_python_initialized = false;
static PyObject*        g_strategy_class = nullptr;  // The UserStrategy class
static PyObject*        g_sdk_module = nullptr;       // SDK module ref

static constexpr int    MAX_INSTANCES = 256;
static PyStrategyInstance g_instances[MAX_INSTANCES];
static int              g_instance_count = 0;

// Platform API pointer (set during on_add, used by callbacks)
static const PlatformAPI* g_platform_api = nullptr;

// ═══════════════════════════════════════════════════════════════════
// PYTHON INITIALIZATION - Run once, embed SDK + strategy code
// ═══════════════════════════════════════════════════════════════════

static bool ensure_python_initialized() {
    if (g_python_initialized) return true;

    Py_Initialize();
    if (!Py_IsInitialized()) {
        fprintf(stderr, "[PY_WRAPPER] Py_Initialize failed\n");
        return false;
    }

    // Step 1: Create the sdk.python_sdk module in-memory
    // We exec the SDK code into a module so the strategy can import it
    PyObject* sdk_pkg = PyImport_AddModule("sdk");
    if (!sdk_pkg) {
        fprintf(stderr, "[PY_WRAPPER] Failed to create sdk package\n");
        PyErr_Print();
        return false;
    }
    // Mark sdk as a package
    PyObject* sdk_dict = PyModule_GetDict(sdk_pkg);
    PyDict_SetItemString(sdk_dict, "__path__", PyList_New(0));
    PyDict_SetItemString(sdk_dict, "__package__", PyUnicode_FromString("sdk"));
    PyObject* sys_modules = PySys_GetObject("modules");
    PyDict_SetItemString(sys_modules, "sdk", sdk_pkg);

    // Create sdk.python_sdk submodule
    PyObject* sdk_mod = PyImport_AddModule("sdk.python_sdk");
    if (!sdk_mod) {
        fprintf(stderr, "[PY_WRAPPER] Failed to create sdk.python_sdk module\n");
        PyErr_Print();
        return false;
    }
    PyObject* sdk_mod_dict = PyModule_GetDict(sdk_mod);
    PyDict_SetItemString(sdk_mod_dict, "__package__", PyUnicode_FromString("sdk"));
    PyDict_SetItemString(sys_modules, "sdk.python_sdk", sdk_mod);

    // Execute the SDK code into sdk.python_sdk
    PyObject* result = PyRun_String(
        EMBEDDED_SDK_CODE,
        Py_file_input,
        sdk_mod_dict,
        sdk_mod_dict
    );
    if (!result) {
        fprintf(stderr, "[PY_WRAPPER] Failed to execute SDK code\n");
        PyErr_Print();
        return false;
    }
    Py_DECREF(result);
    g_sdk_module = sdk_mod;

    // Step 2: Execute the strategy code in __main__
    PyObject* main_mod = PyImport_AddModule("__main__");
    PyObject* main_dict = PyModule_GetDict(main_mod);

    result = PyRun_String(
        EMBEDDED_STRATEGY_CODE,
        Py_file_input,
        main_dict,
        main_dict
    );
    if (!result) {
        fprintf(stderr, "[PY_WRAPPER] Failed to execute strategy code\n");
        PyErr_Print();
        return false;
    }
    Py_DECREF(result);

    // Step 3: Find the UserStrategy class
    g_strategy_class = PyDict_GetItemString(main_dict, "UserStrategy");
    if (!g_strategy_class || !PyCallable_Check(g_strategy_class)) {
        fprintf(stderr, "[PY_WRAPPER] UserStrategy class not found in strategy code\n");

        // Try to find any class that inherits from StrategyAPI
        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(main_dict, &pos, &key, &value)) {
            if (PyType_Check(value) && value != PyDict_GetItemString(sdk_mod_dict, "StrategyAPI")) {
                // Check if it's a subclass of StrategyAPI
                PyObject* strategy_api_cls = PyDict_GetItemString(sdk_mod_dict, "StrategyAPI");
                if (strategy_api_cls && PyObject_IsSubclass(value, strategy_api_cls)) {
                    g_strategy_class = value;
                    fprintf(stderr, "[PY_WRAPPER] Found strategy class: %s\n",
                            PyUnicode_AsUTF8(PyObject_GetAttrString(value, "__name__")));
                    break;
                }
            }
        }

        if (!g_strategy_class) {
            fprintf(stderr, "[PY_WRAPPER] No strategy class found at all\n");
            return false;
        }
    }

    Py_INCREF(g_strategy_class);

    // Init instance slots
    for (int i = 0; i < MAX_INSTANCES; i++) {
        g_instances[i].instance = nullptr;
        g_instances[i].active = false;
    }

    g_python_initialized = true;
    fprintf(stderr, "[PY_WRAPPER] Python initialized successfully\n");
    return true;
}


// ═══════════════════════════════════════════════════════════════════
// HELPER: Find instance by handle pointer
// We use the index as the handle (cast to void*)
// ═══════════════════════════════════════════════════════════════════

static inline PyStrategyInstance* get_instance(void* handle) {
    uintptr_t idx = (uintptr_t)handle;
    if (idx == 0 || idx > MAX_INSTANCES) return nullptr;
    idx--;  // handle is 1-based
    if (!g_instances[idx].active) return nullptr;
    return &g_instances[idx];
}

// ═══════════════════════════════════════════════════════════════════
// HELPER: Call Python method, return response string
// ═══════════════════════════════════════════════════════════════════

static int32_t call_py_method_with_response(
    PyObject* instance,
    const char* method,
    PyObject* args,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    if (!instance) return -1;

    PyObject* result = PyObject_CallMethod(instance, method, nullptr);
    
    // If method takes no args, try without args first
    // For methods with args, the caller should use PyObject_CallMethodObjArgs
    if (!result && args) {
        PyErr_Clear();
        result = PyObject_CallMethodObjArgs(instance, PyUnicode_FromString(method), args, nullptr);
    }

    if (!result) {
        PyErr_Print();
        const char* err = "Python method call failed";
        if (response_out && response_len_out) {
            uint32_t len = strlen(err);
            memcpy(response_out, err, len);
            *response_len_out = len;
        }
        return -1;
    }

    // Extract string response
    if (response_out && response_len_out) {
        PyObject* str_result = nullptr;
        if (PyUnicode_Check(result)) {
            str_result = result;
            Py_INCREF(str_result);
        } else if (PyDict_Check(result)) {
            // Convert dict to JSON-like string
            PyObject* json_mod = PyImport_ImportModule("json");
            if (json_mod) {
                str_result = PyObject_CallMethod(json_mod, "dumps", "O", result);
                Py_DECREF(json_mod);
            }
        } else {
            str_result = PyObject_Str(result);
        }

        if (str_result) {
            const char* resp = PyUnicode_AsUTF8(str_result);
            if (resp) {
                uint32_t len = strlen(resp);
                if (len > 4090) len = 4090;
                memcpy(response_out, resp, len);
                *response_len_out = len;
            }
            Py_DECREF(str_result);
        }
    }

    Py_DECREF(result);
    return 0;
}


// ═══════════════════════════════════════════════════════════════════
// C CALLBACKS → PYTHON CALLS
// ═══════════════════════════════════════════════════════════════════

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
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance) return -1;

    // Store platform API for this strategy's use
    g_platform_api = api;
    inst->pf_id = pf_id;

    // Set pf_id on the Python instance
    PyObject_SetAttrString(inst->instance, "pf_id", PyLong_FromUnsignedLong(pf_id));

    // Store platform context and API as capsules on the instance
    PyObject* ctx_capsule = PyCapsule_New((void*)ctx, "platform_context", nullptr);
    PyObject* api_capsule = PyCapsule_New((void*)api, "platform_api", nullptr);
    PyObject_SetAttrString(inst->instance, "_platform_context", ctx_capsule);
    PyObject_SetAttrString(inst->instance, "_platform_api_ptr", api_capsule);
    Py_DECREF(ctx_capsule);
    Py_DECREF(api_capsule);

    // Parse params bytes into a Python dict
    // Try JSON first, fall back to raw bytes
    PyObject* params_dict = nullptr;
    if (params && params_len > 0) {
        // Try to parse as JSON
        PyObject* json_mod = PyImport_ImportModule("json");
        if (json_mod) {
            PyObject* json_str = PyUnicode_DecodeUTF8((const char*)params, params_len, "replace");
            if (json_str) {
                params_dict = PyObject_CallMethod(json_mod, "loads", "O", json_str);
                if (!params_dict) {
                    PyErr_Clear();
                }
                Py_DECREF(json_str);
            }
            Py_DECREF(json_mod);
        }
        // If JSON parse failed, create dict with raw bytes
        if (!params_dict) {
            params_dict = PyDict_New();
            PyDict_SetItemString(params_dict, "raw",
                PyBytes_FromStringAndSize((const char*)params, params_len));
        }
    } else {
        params_dict = PyDict_New();
    }

    // Call strategy.on_add(params_dict)
    PyObject* result = PyObject_CallMethod(inst->instance, "on_add", "O", params_dict);
    Py_DECREF(params_dict);

    if (!result) {
        PyErr_Print();
        const char* err = "on_add failed";
        memcpy(response_out, err, strlen(err));
        *response_len_out = strlen(err);
        return -1;
    }

    // Extract response
    if (PyUnicode_Check(result)) {
        const char* resp = PyUnicode_AsUTF8(result);
        if (resp) {
            uint32_t len = strlen(resp);
            if (len > 4090) len = 4090;
            memcpy(response_out, resp, len);
            *response_len_out = len;
        }
    }

    Py_DECREF(result);
    return 0;
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
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance) return -1;

    PyObject* params_dict = nullptr;
    if (params && params_len > 0) {
        PyObject* json_mod = PyImport_ImportModule("json");
        if (json_mod) {
            PyObject* json_str = PyUnicode_DecodeUTF8((const char*)params, params_len, "replace");
            if (json_str) {
                params_dict = PyObject_CallMethod(json_mod, "loads", "O", json_str);
                if (!params_dict) PyErr_Clear();
                Py_DECREF(json_str);
            }
            Py_DECREF(json_mod);
        }
        if (!params_dict) {
            params_dict = PyDict_New();
        }
    } else {
        params_dict = PyDict_New();
    }

    PyObject* result = PyObject_CallMethod(inst->instance, "on_edit", "O", params_dict);
    Py_DECREF(params_dict);

    if (!result) {
        PyErr_Print();
        return -1;
    }

    if (PyUnicode_Check(result)) {
        const char* resp = PyUnicode_AsUTF8(result);
        if (resp) {
            uint32_t len = strlen(resp);
            if (len > 4090) len = 4090;
            memcpy(response_out, resp, len);
            *response_len_out = len;
        }
    }
    Py_DECREF(result);
    return 0;
}


static int32_t cpp_on_run(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance) return -1;

    PyObject* result = PyObject_CallMethod(inst->instance, "on_run", nullptr);
    if (!result) {
        PyErr_Print();
        return -1;
    }

    if (PyUnicode_Check(result)) {
        const char* resp = PyUnicode_AsUTF8(result);
        if (resp) {
            uint32_t len = strlen(resp);
            if (len > 4090) len = 4090;
            memcpy(response_out, resp, len);
            *response_len_out = len;
        }
    }
    Py_DECREF(result);
    return 0;
}


static int32_t cpp_on_stop(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance) return -1;

    PyObject* result = PyObject_CallMethod(inst->instance, "on_stop", nullptr);
    if (!result) {
        PyErr_Print();
        return -1;
    }

    if (PyUnicode_Check(result)) {
        const char* resp = PyUnicode_AsUTF8(result);
        if (resp) {
            uint32_t len = strlen(resp);
            if (len > 4090) len = 4090;
            memcpy(response_out, resp, len);
            *response_len_out = len;
        }
    }
    Py_DECREF(result);
    return 0;
}


static int32_t cpp_on_remove(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance) return -1;

    PyObject* result = PyObject_CallMethod(inst->instance, "on_remove", nullptr);
    if (!result) {
        PyErr_Print();
        return -1;
    }

    if (PyUnicode_Check(result)) {
        const char* resp = PyUnicode_AsUTF8(result);
        if (resp) {
            uint32_t len = strlen(resp);
            if (len > 4090) len = 4090;
            memcpy(response_out, resp, len);
            *response_len_out = len;
        }
    }
    Py_DECREF(result);

    // Cleanup instance
    Py_DECREF(inst->instance);
    inst->instance = nullptr;
    inst->active = false;
    g_instance_count--;

    return 0;
}


static int32_t cpp_on_query(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    uint8_t* response_out,
    uint32_t* response_len_out
) {
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance) return -1;

    PyObject* result = PyObject_CallMethod(inst->instance, "on_query", nullptr);
    if (!result) {
        PyErr_Print();
        return -1;
    }

    // Convert to JSON string
    PyObject* json_mod = PyImport_ImportModule("json");
    PyObject* json_str = nullptr;
    if (json_mod) {
        json_str = PyObject_CallMethod(json_mod, "dumps", "O", result);
        Py_DECREF(json_mod);
    }
    if (!json_str) {
        json_str = PyObject_Str(result);
    }

    if (json_str) {
        const char* resp = PyUnicode_AsUTF8(json_str);
        if (resp) {
            uint32_t len = strlen(resp);
            if (len > 4090) len = 4090;
            memcpy(response_out, resp, len);
            *response_len_out = len;
        }
        Py_DECREF(json_str);
    }

    Py_DECREF(result);
    return 0;
}


static void cpp_on_market_event(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const MarketEvent* event
) {
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance || !event) return;

    // Build MarketData object that matches the Python SDK
    // Create a MarketData dataclass instance
    PyObject* sdk_mod = PyImport_ImportModule("sdk.python_sdk");
    if (!sdk_mod) { PyErr_Clear(); return; }

    PyObject* md_cls = PyObject_GetAttrString(sdk_mod, "MarketData");
    Py_DECREF(sdk_mod);
    if (!md_cls) { PyErr_Clear(); return; }

    // Build bid/ask lists from the event
    PyObject* bids = PyList_New(5);
    PyObject* asks = PyList_New(5);
    PyObject* bids_qty = PyList_New(5);
    PyObject* asks_qty = PyList_New(5);

    for (int i = 0; i < 5; i++) {
        PyList_SET_ITEM(bids, i, PyLong_FromUnsignedLong(event->bids[i]));
        PyList_SET_ITEM(asks, i, PyLong_FromUnsignedLong(event->asks[i]));
        PyList_SET_ITEM(bids_qty, i, PyLong_FromUnsignedLong(event->bids_qty[i]));
        PyList_SET_ITEM(asks_qty, i, PyLong_FromUnsignedLong(event->asks_qty[i]));
    }

    // MarketData(token, bids, asks, bids_qty, asks_qty, seqno, ltp, event_time)
    PyObject* md = PyObject_CallFunction(
        md_cls, "IOOOOIII",
        event->token,
        bids, asks, bids_qty, asks_qty,
        event->seqno,
        event->last_traded_price,
        (unsigned int)(event->event_time & 0xFFFFFFFF)
    );
    Py_DECREF(md_cls);

    if (!md) {
        PyErr_Print();
        Py_DECREF(bids); Py_DECREF(asks);
        Py_DECREF(bids_qty); Py_DECREF(asks_qty);
        return;
    }

    // Also set event_time as full 64-bit
    PyObject_SetAttrString(md, "event_time",
        PyLong_FromUnsignedLongLong(event->event_time));

    PyObject* result = PyObject_CallMethod(
        inst->instance, "on_market_event", "O", md);

    Py_XDECREF(result);
    if (!result) PyErr_Print();
    Py_DECREF(md);
}


static void cpp_on_order_update(
    void* handle,
    PlatformContext* ctx,
    uint32_t pf_id,
    const OrderUpdate* update
) {
    PyStrategyInstance* inst = get_instance(handle);
    if (!inst || !inst->instance || !update) return;

    // Build OrderUpdate dataclass
    PyObject* sdk_mod = PyImport_ImportModule("sdk.python_sdk");
    if (!sdk_mod) { PyErr_Clear(); return; }

    PyObject* ou_cls = PyObject_GetAttrString(sdk_mod, "OrderUpdate");
    Py_DECREF(sdk_mod);
    if (!ou_cls) { PyErr_Clear(); return; }

    // OrderUpdate(oms_order_id, exchange_order_id, token, side, state,
    //             ordered_price, ordered_qty, filled_qty, avg_fill_price)
    PyObject* ou = PyObject_CallFunction(
        ou_cls, "IKIbbllll",
        update->oms_order_id,
        update->exchange_order_id,
        update->token,
        update->side,
        (uint8_t)update->state,
        (long)update->ordered_price,
        (long)update->ordered_qty,
        (long)update->filled_qty,
        (long)update->avg_fill_price
    );
    Py_DECREF(ou_cls);

    if (!ou) {
        PyErr_Print();
        return;
    }

    PyObject* result = PyObject_CallMethod(
        inst->instance, "on_order_update", "O", ou);

    Py_XDECREF(result);
    if (!result) PyErr_Print();
    Py_DECREF(ou);
}


// ═══════════════════════════════════════════════════════════════════
// PYTHON → C PLATFORM CALLS (subscribe, place_order, etc.)
// These are set on the Python instance so it can call back to C++
// ═══════════════════════════════════════════════════════════════════

// We implement these as Python C extension methods that the wrapper
// monkey-patches onto the strategy instance.

static PyObject* py_subscribe_token(PyObject* self, PyObject* args) {
    uint32_t token;
    if (!PyArg_ParseTuple(args, "I", &token)) return nullptr;

    // Get pf_id and context from self
    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    if (!pf_obj || !ctx_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap);
        Py_RETURN_NONE;
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap);

    if (ctx) {
        platform_subscribe_token(ctx, pf_id, token);
    }
    Py_RETURN_NONE;
}

static PyObject* py_unsubscribe_token(PyObject* self, PyObject* args) {
    uint32_t token;
    if (!PyArg_ParseTuple(args, "I", &token)) return nullptr;

    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    if (!pf_obj || !ctx_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap);
        Py_RETURN_NONE;
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap);

    if (ctx) {
        platform_unsubscribe_token(ctx, pf_id, token);
    }
    Py_RETURN_NONE;
}

static PyObject* py_place_orders(PyObject* self, PyObject* args) {
    PyObject* order_list;
    int order_type_int = 1;  // Default IOC
    unsigned long long event_time = 0;

    if (!PyArg_ParseTuple(args, "O|iK", &order_list, &order_type_int, &event_time))
        return nullptr;

    if (!PyList_Check(order_list)) {
        PyErr_SetString(PyExc_TypeError, "Expected list of orders");
        return nullptr;
    }

    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    PyObject* api_cap = PyObject_GetAttrString(self, "_platform_api_ptr");
    if (!pf_obj || !ctx_cap || !api_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap); Py_XDECREF(api_cap);
        return PyLong_FromLong(-1);
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    const PlatformAPI* api = (const PlatformAPI*)PyCapsule_GetPointer(api_cap, "platform_api");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap); Py_DECREF(api_cap);

    if (!ctx || !api || !api->place_new_order_multi_leg) {
        return PyLong_FromLong(-1);
    }

    Py_ssize_t count = PyList_Size(order_list);
    if (count <= 0 || count > 3) {
        return PyLong_FromLong(-4);
    }

    Leg legs[3];
    memset(legs, 0, sizeof(legs));

    for (Py_ssize_t i = 0; i < count; i++) {
        PyObject* order = PyList_GetItem(order_list, i);  // borrowed ref

        PyObject* sym = PyObject_GetAttrString(order, "symbol_id");
        PyObject* price = PyObject_GetAttrString(order, "price");
        PyObject* qty = PyObject_GetAttrString(order, "qty");
        PyObject* side = PyObject_GetAttrString(order, "side");

        if (!sym || !price || !qty || !side) {
            Py_XDECREF(sym); Py_XDECREF(price);
            Py_XDECREF(qty); Py_XDECREF(side);
            return PyLong_FromLong(-5);
        }

        legs[i].symbol_id = PyLong_AsUnsignedLong(sym);
        legs[i].price = PyLong_AsUnsignedLong(price);
        legs[i].qty = PyLong_AsUnsignedLong(qty);
        legs[i].side = (OrderSide)PyLong_AsLong(side);
        legs[i].start_time = event_time;

        Py_DECREF(sym); Py_DECREF(price);
        Py_DECREF(qty); Py_DECREF(side);
    }

    int32_t ret = api->place_new_order_multi_leg(
        ctx, pf_id, legs, (uint8_t)count, (OrderType)order_type_int, event_time);

    return PyLong_FromLong(ret);
}

static PyObject* py_modify_order(PyObject* self, PyObject* args) {
    uint32_t oms_order_id;
    int64_t new_price;
    int32_t new_qty;

    if (!PyArg_ParseTuple(args, "Ili", &oms_order_id, &new_price, &new_qty))
        return nullptr;

    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    PyObject* api_cap = PyObject_GetAttrString(self, "_platform_api_ptr");
    if (!pf_obj || !ctx_cap || !api_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap); Py_XDECREF(api_cap);
        return PyLong_FromLong(-1);
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    const PlatformAPI* api = (const PlatformAPI*)PyCapsule_GetPointer(api_cap, "platform_api");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap); Py_DECREF(api_cap);

    if (!ctx || !api || !api->place_modify_order)
        return PyLong_FromLong(-1);

    int32_t ret = api->place_modify_order(ctx, pf_id, oms_order_id, new_price, new_qty);
    return PyLong_FromLong(ret);
}

static PyObject* py_cancel_order(PyObject* self, PyObject* args) {
    uint32_t oms_order_id;
    if (!PyArg_ParseTuple(args, "I", &oms_order_id))
        return nullptr;

    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    PyObject* api_cap = PyObject_GetAttrString(self, "_platform_api_ptr");
    if (!pf_obj || !ctx_cap || !api_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap); Py_XDECREF(api_cap);
        return PyLong_FromLong(-1);
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    const PlatformAPI* api = (const PlatformAPI*)PyCapsule_GetPointer(api_cap, "platform_api");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap); Py_DECREF(api_cap);

    if (!ctx || !api || !api->place_cancel_order)
        return PyLong_FromLong(-1);

    int32_t ret = api->place_cancel_order(ctx, pf_id, oms_order_id);
    return PyLong_FromLong(ret);
}

static PyObject* py_log_msg(PyObject* self, PyObject* args) {
    const char* msg;
    if (!PyArg_ParseTuple(args, "s", &msg))
        return nullptr;

    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    PyObject* api_cap = PyObject_GetAttrString(self, "_platform_api_ptr");
    if (!pf_obj || !ctx_cap || !api_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap); Py_XDECREF(api_cap);
        Py_RETURN_NONE;
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    const PlatformAPI* api = (const PlatformAPI*)PyCapsule_GetPointer(api_cap, "platform_api");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap); Py_DECREF(api_cap);

    if (ctx && api && api->log_msg) {
        api->log_msg(ctx, pf_id, msg, strlen(msg));
    }
    Py_RETURN_NONE;
}

static PyObject* py_send_status(PyObject* self, PyObject* args) {
    int32_t traded_qty, achieved_spread, current_spread;
    int is_complete, has_opportunity;

    if (!PyArg_ParseTuple(args, "iiipp", &traded_qty, &achieved_spread,
                          &current_spread, &is_complete, &has_opportunity))
        return nullptr;

    PyObject* pf_obj = PyObject_GetAttrString(self, "pf_id");
    PyObject* ctx_cap = PyObject_GetAttrString(self, "_platform_context");
    PyObject* api_cap = PyObject_GetAttrString(self, "_platform_api_ptr");
    if (!pf_obj || !ctx_cap || !api_cap) {
        Py_XDECREF(pf_obj); Py_XDECREF(ctx_cap); Py_XDECREF(api_cap);
        Py_RETURN_NONE;
    }

    uint32_t pf_id = PyLong_AsUnsignedLong(pf_obj);
    PlatformContext* ctx = (PlatformContext*)PyCapsule_GetPointer(ctx_cap, "platform_context");
    const PlatformAPI* api = (const PlatformAPI*)PyCapsule_GetPointer(api_cap, "platform_api");
    Py_DECREF(pf_obj); Py_DECREF(ctx_cap); Py_DECREF(api_cap);

    if (ctx && api && api->send_status_update) {
        StrategyStatusUpdate status;
        memset(&status, 0, sizeof(status));
        status.pf_id = pf_id;
        status.traded_qty = traded_qty;
        status.achieved_spread = achieved_spread;
        status.current_spread = current_spread;
        status.is_complete = (bool)is_complete;
        status.has_opportunity = (bool)has_opportunity;
        api->send_status_update(ctx, pf_id, &status);
    }
    Py_RETURN_NONE;
}


// Method table for binding C functions to Python instance
static PyMethodDef g_bound_methods[] = {
    {"subscribe_token",   (PyCFunction)py_subscribe_token,   METH_VARARGS, "Subscribe to token"},
    {"unsubscribe_token", (PyCFunction)py_unsubscribe_token, METH_VARARGS, "Unsubscribe from token"},
    {"place_orders",      (PyCFunction)py_place_orders,      METH_VARARGS, "Place multi-leg order"},
    {"modify_order",      (PyCFunction)py_modify_order,      METH_VARARGS, "Modify order"},
    {"cancel_order",      (PyCFunction)py_cancel_order,      METH_VARARGS, "Cancel order"},
    {"log",               (PyCFunction)py_log_msg,           METH_VARARGS, "Log message"},
    {"send_status",       (PyCFunction)py_send_status,       METH_VARARGS, "Send status update"},
    {nullptr, nullptr, 0, nullptr}
};


// Bind C methods onto a Python strategy instance.
// We use PyCFunction_New with the instance as 'self' — this creates a
// builtin_function_or_method where self is passed as first arg to the C function.
// When Python calls instance.subscribe_token(token), the C function receives
// (self=instance, args=(token,)).
static void bind_platform_methods(PyObject* instance) {
    for (int i = 0; g_bound_methods[i].ml_name != nullptr; i++) {
        // PyCFunction_New(def, self) creates a PyCFunction where 'self' is 
        // automatically passed as the first argument. Do NOT wrap in PyMethod_New
        // or self gets passed twice.
        PyObject* cfunc = PyCFunction_New(&g_bound_methods[i], instance);
        if (cfunc) {
            PyObject_SetAttrString(instance, g_bound_methods[i].ml_name, cfunc);
            Py_DECREF(cfunc);
        }
    }
}


// ═══════════════════════════════════════════════════════════════════
// EXPORTED C INTERFACE (C++ platform calls these via dlsym)
// ═══════════════════════════════════════════════════════════════════

extern "C" {

uint32_t strategy_get_type_id(void) {
    return STRATEGY_TYPE_ID;
}


void* strategy_create(StrategyFnTable* tbl_out) {
    if (!ensure_python_initialized()) {
        fprintf(stderr, "[PY_WRAPPER] Python init failed in strategy_create\n");
        return nullptr;
    }

    if (!g_strategy_class) {
        fprintf(stderr, "[PY_WRAPPER] No strategy class available\n");
        return nullptr;
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!g_instances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "[PY_WRAPPER] No free instance slots\n");
        return nullptr;
    }

    // Create Python strategy instance
    PyObject* instance = PyObject_CallObject(g_strategy_class, nullptr);
    if (!instance) {
        fprintf(stderr, "[PY_WRAPPER] Failed to instantiate strategy class\n");
        PyErr_Print();
        return nullptr;
    }

    // Bind C platform methods to this instance
    bind_platform_methods(instance);

    // Store
    g_instances[slot].instance = instance;
    g_instances[slot].active = true;
    g_instances[slot].pf_id = 0;
    g_instance_count++;

    // Fill function table
    tbl_out->on_add          = cpp_on_add;
    tbl_out->on_edit         = cpp_on_edit;
    tbl_out->on_run          = cpp_on_run;
    tbl_out->on_stop         = cpp_on_stop;
    tbl_out->on_remove       = cpp_on_remove;
    tbl_out->on_query        = cpp_on_query;
    tbl_out->on_market_event = cpp_on_market_event;
    tbl_out->on_order_update = cpp_on_order_update;

    // Return 1-based handle (0 would be null)
    void* handle = (void*)(uintptr_t)(slot + 1);
    fprintf(stderr, "[PY_WRAPPER] Created instance in slot %d (handle=%p)\n", slot, handle);
    return handle;
}


void strategy_destroy_all(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (g_instances[i].active && g_instances[i].instance) {
            Py_DECREF(g_instances[i].instance);
            g_instances[i].instance = nullptr;
            g_instances[i].active = false;
        }
    }
    g_instance_count = 0;

    Py_XDECREF(g_strategy_class);
    g_strategy_class = nullptr;

    if (g_python_initialized) {
        Py_Finalize();
        g_python_initialized = false;
    }
}

}  // extern "C"
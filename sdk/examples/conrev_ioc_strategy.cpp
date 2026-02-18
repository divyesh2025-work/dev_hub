#include "../strategy_sdk.h"
#include <string.h>
#include <stdio.h>
#include <iostream>

#define CONREV_IOC_TYPE_ID 100

/* ═══════════════════════════════════════════════════════════
 * PARAMS STRUCTURE (frontend serializes this)
 * ═══════════════════════════════════════════════════════════ */

struct ConRevIOCParams {
    uint32_t fut_token;
    uint32_t call_token;
    uint32_t put_token;
    int32_t  strike_price;
    int32_t  max_lots;
    int32_t  spread_threshold;
    bool     is_conversion;
} __attribute__((packed));

/* ═══════════════════════════════════════════════════════════
 * STRATEGY STATE (private)
 * ═══════════════════════════════════════════════════════════ */

struct ConRevIOCState {
    const PlatformAPI* api;
    PlatformContext*   ctx;
    uint32_t           pf_id;
    
    ConRevIOCParams    params;
    
    // Runtime state
    bool     active;
    int32_t  traded_qty;
    int32_t  achieved_spread;
    
    uint32_t fut_oms_id;
    uint32_t call_oms_id;
    uint32_t put_oms_id;
    
    bool     legs_sent;
    
    // Cached market data (since we're subscribed to 3 tokens)
    MarketEvent fut_market;
    MarketEvent call_market;
    MarketEvent put_market;
    bool        fut_valid;
    bool        call_valid;
    bool        put_valid;
};

// Pre-allocated pool
static ConRevIOCState g_pool[512];
static uint32_t       g_alloc = 0;

/* ═══════════════════════════════════════════════════════════
 * HELPER: Compute spread
 * ═══════════════════════════════════════════════════════════ */

static bool compute_opportunity(ConRevIOCState* s, int32_t* spread_out)
{
    if (!s->fut_valid || !s->call_valid || !s->put_valid) return false;
    
    int64_t fut_bid = s->fut_market.bids[0].price;
    int64_t fut_ask = s->fut_market.asks[0].price;
    int64_t call_bid = s->call_market.bids[0].price;
    int64_t call_ask = s->call_market.asks[0].price;
    int64_t put_bid = s->put_market.bids[0].price;
    int64_t put_ask = s->put_market.asks[0].price;
    
    int32_t spread;
    if (s->params.is_conversion) {
        spread = s->params.strike_price - fut_ask + call_bid - put_ask;
    } else {
        spread = -s->params.strike_price + fut_bid - call_ask + put_bid;
    }
    
    *spread_out = spread;
    return spread >= s->params.spread_threshold;
}

/* ═══════════════════════════════════════════════════════════
 * FRONTEND CALLBACKS (lifecycle management)
 * ═══════════════════════════════════════════════════════════ */

static int32_t conrev_on_add(void* handle, PlatformContext* ctx,
                             const PlatformAPI* api, uint32_t pf_id,
                             const uint8_t* params, uint32_t params_len,
                             uint8_t* response_out, uint32_t* response_len_out)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    // Validate params
    if (params_len != sizeof(ConRevIOCParams)) {
        const char* err = "Invalid params size";
        memcpy(response_out, err, strlen(err));
        *response_len_out = strlen(err);
        return -1;
    }
    
    // Parse params
    memcpy(&s->params, params, sizeof(ConRevIOCParams));
    
    // Initialize state
    s->api = api;
    s->ctx = ctx;
    s->pf_id = pf_id;
    s->active = false;
    s->traded_qty = 0;
    s->achieved_spread = 0;
    s->legs_sent = false;
    s->fut_valid = false;
    s->call_valid = false;
    s->put_valid = false;
    
    // Log
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf),
             "ADD: pf=%u fut=%u call=%u put=%u strike=%d max=%d spread=%d conv=%d",
             pf_id, s->params.fut_token, s->params.call_token, s->params.put_token,
             s->params.strike_price, s->params.max_lots, 
             s->params.spread_threshold, s->params.is_conversion);
    s->api->log_msg(ctx, pf_id, log_buf, strlen(log_buf));
    
    // Response
    const char* ok = "Portfolio added";
    memcpy(response_out, ok, strlen(ok));
    *response_len_out = strlen(ok);

    std::cout<<"Here is the added call\n";
    
    return 0;
}

static int32_t conrev_on_edit(void* handle, PlatformContext* ctx, uint32_t pf_id,
                              const uint8_t* params, uint32_t params_len,
                              uint8_t* response_out, uint32_t* response_len_out)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    if (params_len != sizeof(ConRevIOCParams)) {
        const char* err = "Invalid params size";
        memcpy(response_out, err, strlen(err));
        *response_len_out = strlen(err);
        return -1;
    }
    
    // Don't allow edit of tokens/strike (only runtime params)
    ConRevIOCParams new_params;
    memcpy(&new_params, params, sizeof(ConRevIOCParams));
    
    if (new_params.fut_token != s->params.fut_token ||
        new_params.call_token != s->params.call_token ||
        new_params.put_token != s->params.put_token ||
        new_params.strike_price != s->params.strike_price)
    {
        const char* err = "Cannot edit tokens/strike after creation";
        memcpy(response_out, err, strlen(err));
        *response_len_out = strlen(err);
        return -1;
    }
    
    // Update runtime params
    s->params.max_lots = new_params.max_lots;
    s->params.spread_threshold = new_params.spread_threshold;
    s->params.is_conversion = new_params.is_conversion;
    
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf),
             "EDIT: pf=%u max=%d spread=%d conv=%d",
             pf_id, s->params.max_lots, s->params.spread_threshold, 
             s->params.is_conversion);
    s->api->log_msg(ctx, pf_id, log_buf, strlen(log_buf));
    
    const char* ok = "Parameters updated";
    memcpy(response_out, ok, strlen(ok));
    *response_len_out = strlen(ok);
    
    return 0;
}

static int32_t conrev_on_run(void* handle, PlatformContext* ctx, uint32_t pf_id,
                             uint8_t* response_out, uint32_t* response_len_out)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    if (s->active) {
        const char* err = "Already running";
        memcpy(response_out, err, strlen(err));
        *response_len_out = strlen(err);
        return -1;
    }
    
    // Subscribe to all tokens
    platform_subscribe_token(ctx, pf_id, s->params.fut_token);
    platform_subscribe_token(ctx, pf_id, s->params.call_token);
    platform_subscribe_token(ctx, pf_id, s->params.put_token);
    
    s->active = true;
    s->legs_sent = false;
    
    s->api->log_msg(ctx, pf_id, "RUN: Strategy started", 19);
    
    const char* ok = "Strategy running";
    memcpy(response_out, ok, strlen(ok));
    *response_len_out = strlen(ok);
    std::cout<<"Here is the run call\n";

    
    return 0;
}

static int32_t conrev_on_stop(void* handle, PlatformContext* ctx, uint32_t pf_id,
                              uint8_t* response_out, uint32_t* response_len_out)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    if (!s->active) {
        const char* err = "Not running";
        memcpy(response_out, err, strlen(err));
        *response_len_out = strlen(err);
        return -1;
    }
    
    // Cancel pending orders
    if (s->fut_oms_id)  s->api->place_cancel_order(ctx, pf_id, s->fut_oms_id);
    if (s->call_oms_id) s->api->place_cancel_order(ctx, pf_id, s->call_oms_id);
    if (s->put_oms_id)  s->api->place_cancel_order(ctx, pf_id, s->put_oms_id);
    
    // Unsubscribe
    platform_unsubscribe_token(ctx, pf_id, s->params.fut_token);
    platform_unsubscribe_token(ctx, pf_id, s->params.call_token);
    platform_unsubscribe_token(ctx, pf_id, s->params.put_token);
    
    s->active = false;
    
    s->api->log_msg(ctx, pf_id, "STOP: Strategy stopped", 21);
    
    const char* ok = "Strategy stopped";
    memcpy(response_out, ok, strlen(ok));
    *response_len_out = strlen(ok);
    
    return 0;
}

static int32_t conrev_on_remove(void* handle, PlatformContext* ctx, uint32_t pf_id,
                                uint8_t* response_out, uint32_t* response_len_out)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    // Stop first if running
    if (s->active) {
        conrev_on_stop(handle, ctx, pf_id, response_out, response_len_out);
    }
    
    // Reset state (but don't free - using static pool)
    memset(s, 0, sizeof(*s));
    
    const char* ok = "Portfolio removed";
    memcpy(response_out, ok, strlen(ok));
    *response_len_out = strlen(ok);
    
    return 0;
}

static int32_t conrev_on_query(void* handle, PlatformContext* ctx, uint32_t pf_id,
                               uint8_t* response_out, uint32_t* response_len_out)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    // Build JSON response
    char json[512];
    snprintf(json, sizeof(json),
             "{\"pf_id\":%u,\"active\":%d,\"traded_qty\":%d,\"max_lots\":%d,"
             "\"achieved_spread\":%d,\"legs_sent\":%d}",
             pf_id, s->active, s->traded_qty, s->params.max_lots,
             s->achieved_spread, s->legs_sent);
    
    memcpy(response_out, json, strlen(json));
    *response_len_out = strlen(json);
    
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * HOT PATH CALLBACKS
 * ═══════════════════════════════════════════════════════════ */

static void conrev_on_market_event(void* handle, PlatformContext* ctx,
                                   uint32_t pf_id, const MarketEvent* ev)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    if (!s->active) return;
    
    // Cache market data
    if (ev->token == s->params.fut_token) {
        s->fut_market = *ev;
        s->fut_valid = true;
    } else if (ev->token == s->params.call_token) {
        s->call_market = *ev;
        s->call_valid = true;
    } else if (ev->token == s->params.put_token) {
        s->put_market = *ev;
        s->put_valid = true;
    }
    
    // ═══════════════════════════════════════════════════════
    // NEW: ALWAYS COMPUTE AND SEND CURRENT SPREAD
    // ═══════════════════════════════════════════════════════
    int32_t current_spread = 0;
    bool has_opportunity = compute_opportunity(s, &current_spread);
    
    // Send status update with current spread
    StrategyStatusUpdate status;
    status.pf_id = pf_id;
    status.traded_qty = s->traded_qty;
    status.achieved_spread = s->achieved_spread;
    status.current_spread = current_spread;      // NEW
    status.has_opportunity = has_opportunity;    // NEW
    status.is_complete = (s->traded_qty >= s->params.max_lots);
    status.custom_data_len = 0;
    
    s->api->send_status_update(ctx, pf_id, &status);
    // ═══════════════════════════════════════════════════════
    
    // Only place orders if:
    // 1. Not already sent (IOC = one shot)
    // 2. Opportunity exists
    // 3. Not yet at max lots
    if (s->legs_sent) return;
    if (s->traded_qty >= s->params.max_lots) return;
    if (!has_opportunity) return;
    
    // Log opportunity
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf),
             "OPPORTUNITY: spread=%d (threshold=%d)",
             current_spread, s->params.spread_threshold);
    s->api->log_msg(ctx, pf_id, log_buf, strlen(log_buf));
    
    // Place all 3 legs
    int32_t qty = s->params.max_lots - s->traded_qty;
    
    if (s->params.is_conversion) {
        // Long Fut + Short Call + Long Put
        s->fut_oms_id  = s->api->place_new_order(ctx, pf_id, s->params.fut_token,  
                                                  0, s->fut_market.asks[0].price, qty);
        s->call_oms_id = s->api->place_new_order(ctx, pf_id, s->params.call_token, 
                                                  1, s->call_market.bids[0].price, qty);
        s->put_oms_id  = s->api->place_new_order(ctx, pf_id, s->params.put_token,  
                                                  0, s->put_market.asks[0].price, qty);
    } else {
        // Short Fut + Long Call + Short Put
        s->fut_oms_id  = s->api->place_new_order(ctx, pf_id, s->params.fut_token,  
                                                  1, s->fut_market.bids[0].price, qty);
        s->call_oms_id = s->api->place_new_order(ctx, pf_id, s->params.call_token, 
                                                  0, s->call_market.asks[0].price, qty);
        s->put_oms_id  = s->api->place_new_order(ctx, pf_id, s->params.put_token,  
                                                  1, s->put_market.bids[0].price, qty);
    }
    
    s->legs_sent = true;
    s->achieved_spread = current_spread;  // Store the spread at which we placed
    
    snprintf(log_buf, sizeof(log_buf),
             "ORDERS_PLACED: fut=%lu call=%lu put=%lu qty=%d spread=%d",
             s->fut_oms_id, s->call_oms_id, s->put_oms_id, qty, current_spread);
    s->api->log_msg(ctx, pf_id, log_buf, strlen(log_buf));
}

static void conrev_on_order_update(void* handle, PlatformContext* ctx,
                                   uint32_t pf_id, const OrderUpdate* upd)
{
    ConRevIOCState* s = (ConRevIOCState*)handle;
    
    if (upd->state == Fill || upd->state == PartialFill) {
        s->traded_qty += upd->filled_qty;
        
        // Check if complete
        if (s->traded_qty >= s->params.max_lots) {
            char log_buf[64];
            snprintf(log_buf, sizeof(log_buf),
                     "COMPLETE: traded=%d spread=%d",
                     s->traded_qty, s->achieved_spread);
            s->api->log_msg(ctx, pf_id, log_buf, strlen(log_buf));
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 * STRATEGY EXPORTS
 * ═══════════════════════════════════════════════════════════ */

extern "C" {

uint32_t strategy_get_type_id(void)
{
    return CONREV_IOC_TYPE_ID;
}

void* strategy_create(StrategyFnTable* tbl)
{
    if (g_alloc >= 512) return NULL;
    
    ConRevIOCState* s = &g_pool[g_alloc++];
    memset(s, 0, sizeof(*s));
    
    tbl->on_add          = conrev_on_add;
    tbl->on_edit         = conrev_on_edit;
    tbl->on_run          = conrev_on_run;
    tbl->on_stop         = conrev_on_stop;
    tbl->on_remove       = conrev_on_remove;
    tbl->on_query        = conrev_on_query;
    tbl->on_market_event = conrev_on_market_event;
    tbl->on_order_update = conrev_on_order_update;
    
    return s;
}

void strategy_destroy_all(void)
{
    g_alloc = 0;
}

} // extern "C"
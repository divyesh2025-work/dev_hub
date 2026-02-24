# base_strategy.pxd
from strategy_types cimport *
from platform_api cimport PlatformAPIWrapper

cdef class BaseStrategy:
    cdef public PlatformAPIWrapper api
    cdef public uint32_t pf_id
    cdef public object user_state
    
    cdef void _init_api(self, PlatformContext* ctx, const PlatformAPI* c_api, uint32_t pf_id)
    cpdef str on_add(self, dict params)
    cpdef str on_edit(self, dict params)
    cpdef str on_run(self)
    cpdef str on_stop(self)
    cpdef str on_remove(self)
    cpdef str on_query(self)
    cpdef void on_market_event(self, dict event)
    cpdef void on_order_update(self, dict update)

# C callback function declarations
cdef int32_t c_on_add(void* handle, PlatformContext* ctx,
                      const PlatformAPI* api, uint32_t pf_id,
                      const uint8_t* params, uint32_t params_len,
                      uint8_t* response_out, uint32_t* response_len_out) noexcept nogil

cdef int32_t c_on_edit(void* handle, PlatformContext* ctx, uint32_t pf_id,
                       const uint8_t* params, uint32_t params_len,
                       uint8_t* response_out, uint32_t* response_len_out) noexcept nogil

cdef int32_t c_on_run(void* handle, PlatformContext* ctx, uint32_t pf_id,
                      uint8_t* response_out, uint32_t* response_len_out) noexcept nogil

cdef int32_t c_on_stop(void* handle, PlatformContext* ctx, uint32_t pf_id,
                       uint8_t* response_out, uint32_t* response_len_out) noexcept nogil

cdef int32_t c_on_remove(void* handle, PlatformContext* ctx, uint32_t pf_id,
                         uint8_t* response_out, uint32_t* response_len_out) noexcept nogil

cdef int32_t c_on_query(void* handle, PlatformContext* ctx, uint32_t pf_id,
                        uint8_t* response_out, uint32_t* response_len_out) noexcept nogil

cdef void c_on_market_event(void* handle, PlatformContext* ctx, uint32_t pf_id,
                            const MarketEvent* event) noexcept nogil

cdef void c_on_order_update(void* handle, PlatformContext* ctx, uint32_t pf_id,
                            const OrderUpdate* update) noexcept nogil
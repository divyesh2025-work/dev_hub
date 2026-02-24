# platform_api.pxd
from strategy_types cimport *

cdef class PlatformAPIWrapper:
    cdef PlatformContext* ctx
    cdef const PlatformAPI* api
    cdef uint32_t pf_id
    
    cdef void _init(self, PlatformContext* ctx, const PlatformAPI* api, uint32_t pf_id) nogil
    cpdef int32_t place_multi_leg_order(self, list legs, OrderType order_type, unsigned long long event_time)
    cpdef int32_t modify_order(self, uint32_t oms_order_id, int64_t new_price, int32_t new_qty)
    cpdef int32_t cancel_order(self, uint32_t oms_order_id)
    cpdef dict get_position(self, uint32_t token)
    cpdef list get_open_orders(self, int32_t max_count=*)
    cpdef void log(self, str message)
    cpdef void send_status(self, dict status)
    cpdef void subscribe_token(self, uint32_t token)
    cpdef void unsubscribe_token(self, uint32_t token)
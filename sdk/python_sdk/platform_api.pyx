# platform_api.pyx
# Python wrapper for PlatformAPI

from strategy_types cimport *
from libc.string cimport memcpy, strlen
from libc.stdint cimport uint8_t, uint32_t, int32_t, int64_t

cdef class PlatformAPIWrapper:
    """Python-friendly wrapper around C PlatformAPI"""
    
    def __cinit__(self):
        self.ctx = NULL
        self.api = NULL
        self.pf_id = 0
    
    cdef void _init(self, PlatformContext* ctx, const PlatformAPI* api, uint32_t pf_id) nogil:
        """Internal initialization from C code"""
        self.ctx = ctx
        self.api = api
        self.pf_id = pf_id
    
    # ═══════════════════════════════════════════════════════════
    # ORDER PLACEMENT
    # ═══════════════════════════════════════════════════════════
    
    cpdef int32_t place_multi_leg_order(self, list legs, OrderType order_type, 
                                        unsigned long long event_time):
        """
        Place multi-leg order
        
        Args:
            legs: List of tuples (token, price, qty, side)
            order_type: OrderType.IOC or OrderType.Bidding
            event_time: Event timestamp
            
        Returns:
            parent_oms_order_id on success, negative on error
        """
        cdef Leg c_legs[10]  # Max 10 legs
        cdef uint8_t leg_count = len(legs)
        
        if leg_count > 10:
            return -1
        
        # Convert Python list to C array
        for i in range(leg_count):
            token, price, qty, side = legs[i]
            c_legs[i].symbol_id = token
            c_legs[i].price = price
            c_legs[i].qty = qty
            c_legs[i].side = <OrderSide>side
            c_legs[i].start_time = 0
            c_legs[i].oms_order_id = 0
        
        cdef int32_t result
        with nogil:
            result = self.api.place_new_order_multi_leg(
                self.ctx, self.pf_id, c_legs, leg_count, order_type, event_time
            )
        
        # Extract assigned oms_order_ids back to Python
        for i in range(leg_count):
            legs[i] = (legs[i][0], legs[i][1], legs[i][2], legs[i][3], c_legs[i].oms_order_id)
        
        return result
    
    cpdef int32_t modify_order(self, uint32_t oms_order_id, int64_t new_price, int32_t new_qty):
        """Modify existing order"""
        cdef int32_t result
        with nogil:
            result = self.api.place_modify_order(
                self.ctx, self.pf_id, oms_order_id, new_price, new_qty
            )
        return result
    
    cpdef int32_t cancel_order(self, uint32_t oms_order_id):
        """Cancel existing order"""
        cdef int32_t result
        with nogil:
            result = self.api.place_cancel_order(self.ctx, self.pf_id, oms_order_id)
        return result
    
    # ═══════════════════════════════════════════════════════════
    # QUERY FUNCTIONS
    # ═══════════════════════════════════════════════════════════
    
    cpdef dict get_position(self, uint32_t token):
        """
        Get position for token
        
        Returns:
            dict with keys: token, net_qty, avg_price, realised_pnl
            or None on error
        """
        cdef PositionView pos
        cdef int32_t result
        
        with nogil:
            result = self.api.get_position(self.ctx, self.pf_id, token, &pos)
        
        if result < 0:
            return None
        
        return {
            'token': pos.token,
            'net_qty': pos.net_qty,
            'avg_price': pos.avg_price,
            'realised_pnl': pos.realised_pnl
        }
    
    cpdef list get_open_orders(self, int32_t max_count=100):
        """
        Get open orders
        
        Returns:
            List of dicts with order details
        """
        cdef OpenOrderView orders[100]
        cdef int32_t count
        
        if max_count > 100:
            max_count = 100
        
        with nogil:
            count = self.api.get_open_orders(self.ctx, self.pf_id, orders, max_count)
        
        if count <= 0:
            return []
        
        result = []
        for i in range(count):
            result.append({
                'oms_order_id': orders[i].oms_order_id,
                'token': orders[i].token,
                'side': orders[i].side,
                'price': orders[i].price,
                'qty': orders[i].qty,
                'state': orders[i].state
            })
        
        return result
    
    # ═══════════════════════════════════════════════════════════
    # LOGGING & STATUS
    # ═══════════════════════════════════════════════════════════
    
    cpdef void log(self, str message):
        """Log message to platform"""
        cdef bytes msg_bytes = message.encode('utf-8')
        cdef const char* msg_ptr = msg_bytes
        cdef uint32_t msg_len = len(msg_bytes)
        
        with nogil:
            self.api.log_msg(self.ctx, self.pf_id, msg_ptr, msg_len)
    
    cpdef void send_status(self, dict status):
        """
        Send status update to frontend
        
        Args:
            status: dict with keys:
                - traded_qty: int
                - achieved_spread: int
                - current_spread: int
                - is_complete: bool
                - has_opportunity: bool
                - custom_data: bytes (optional)
        """
        cdef StrategyStatusUpdate upd
        upd.pf_id = self.pf_id
        upd.traded_qty = status.get('traded_qty', 0)
        upd.achieved_spread = status.get('achieved_spread', 0)
        upd.current_spread = status.get('current_spread', 0)
        upd.is_complete = status.get('is_complete', False)
        upd.has_opportunity = status.get('has_opportunity', False)
        
        custom = status.get('custom_data', b'')
        if isinstance(custom, bytes):
            upd.custom_data_len = min(len(custom), 512)
            memcpy(upd.custom_data, <char*>custom, upd.custom_data_len)
        else:
            upd.custom_data_len = 0
        
        with nogil:
            self.api.send_status_update(self.ctx, self.pf_id, &upd)
    
    # ═══════════════════════════════════════════════════════════
    # TOKEN SUBSCRIPTION
    # ═══════════════════════════════════════════════════════════
    
    cpdef void subscribe_token(self, uint32_t token):
        """Subscribe to market data for token"""
        with nogil:
            platform_subscribe_token(self.ctx, self.pf_id, token)
    
    cpdef void unsubscribe_token(self, uint32_t token):
        """Unsubscribe from market data for token"""
        with nogil:
            platform_unsubscribe_token(self.ctx, self.pf_id, token)

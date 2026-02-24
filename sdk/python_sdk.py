"""
Python Strategy SDK
Provides a high-level Pythonic interface for writing trading strategies
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import List, Tuple, Optional
from enum import IntEnum


class OrderState(IntEnum):
    """Order state enumeration"""
    NEW_OMS = 0
    NEW_EXCHANGE = 1
    MODIFY_OMS = 2
    MODIFY_EXCHANGE = 3
    CANCEL_EXCHANGE = 4
    EXCHANGE_REJECTED = 5
    FILL = 6
    PARTIAL_FILL = 7


class OrderType(IntEnum):
    """Order type enumeration"""
    BIDDING = 0
    IOC = 1  # Immediate or Cancel


class OrderSide(IntEnum):
    """Order side enumeration"""
    BUY = 0
    SELL = 1


@dataclass
class MarketData:
    """Market data snapshot"""
    token: int
    bids: List[int]  # 5 levels
    asks: List[int]  # 5 levels
    bids_qty: List[int]  # 5 levels
    asks_qty: List[int]  # 5 levels
    seqno: int
    last_traded_price: int
    event_time: int

    def bid(self, level: int = 0) -> int:
        """Get bid price at level (default=best bid)"""
        return self.bids[level] if level < len(self.bids) else 0

    def ask(self, level: int = 0) -> int:
        """Get ask price at level (default=best ask)"""
        return self.asks[level] if level < len(self.asks) else 0

    def bid_qty(self, level: int = 0) -> int:
        """Get bid quantity at level"""
        return self.bids_qty[level] if level < len(self.bids_qty) else 0

    def ask_qty(self, level: int = 0) -> int:
        """Get ask quantity at level"""
        return self.asks_qty[level] if level < len(self.asks_qty) else 0

    def spread(self) -> int:
        """Get market spread"""
        return self.asks[0] - self.bids[0] if self.asks and self.bids else 0


@dataclass
class OrderUpdate:
    """Order update notification"""
    oms_order_id: int
    exchange_order_id: int
    token: int
    side: OrderSide
    state: OrderState
    ordered_price: int
    ordered_qty: int
    filled_qty: int
    avg_fill_price: int

    @property
    def pending_qty(self) -> int:
        """Return pending quantity"""
        return self.ordered_qty - self.filled_qty

    @property
    def is_filled(self) -> bool:
        """Check if order is fully filled"""
        return self.filled_qty == self.ordered_qty


@dataclass
class Order:
    """Order specification for placing orders"""
    symbol_id: int
    price: int
    qty: int
    side: OrderSide
    start_time: int = 0


class StrategyAPI(ABC):
    """
    Base class for strategy implementations
    Override methods to define strategy behavior
    """

    def __init__(self):
        """Initialize strategy"""
        self.pf_id: Optional[int] = None
        self.platform_api = None
        self.platform_context = None
        self.params: dict = {}

    @abstractmethod
    def on_add(self, params: dict) -> str:
        """
        Called when portfolio is added
        Parse params and initialize strategy state
        Returns: response message
        """
        pass

    @abstractmethod
    def on_edit(self, params: dict) -> str:
        """
        Called when portfolio is edited
        Update runtime parameters
        Returns: response message
        """
        pass

    @abstractmethod
    def on_run(self) -> str:
        """
        Called to start the strategy
        Subscribe to market data, prepare to trade
        Returns: response message
        """
        pass

    @abstractmethod
    def on_stop(self) -> str:
        """
        Called to stop the strategy
        Cancel pending orders, cleanup resources
        Returns: response message
        """
        pass

    @abstractmethod
    def on_remove(self) -> str:
        """
        Called when portfolio is removed
        Final cleanup
        Returns: response message
        """
        pass

    @abstractmethod
    def on_query(self) -> dict:
        """
        Called to query strategy state
        Returns: status dictionary
        """
        pass

    @abstractmethod
    def on_market_event(self, market: MarketData):
        """
        Called when market data arrives
        Fast path - minimize latency
        """
        pass

    @abstractmethod
    def on_order_update(self, update: OrderUpdate):
        """
        Called when order is updated
        Handle fills, rejections, cancels
        """
        pass

    # ══════════════════════════════════════════════════════════
    # HELPER METHODS
    # ══════════════════════════════════════════════════════════

    def place_orders(self, orders: List[Order], order_type: OrderType = OrderType.IOC, 
                     event_time: int = 0) -> int:
        """
        Place multiple orders atomically
        Returns: parent order ID (>0 on success, <=0 on error)
        """
        if not self.platform_api:
            return -1
        return self.platform_api.place_new_order_multi_leg(
            [self._order_to_leg(o, event_time) for o in orders],
            order_type,
            event_time
        )

    def modify_order(self, oms_order_id: int, new_price: int, new_qty: int) -> int:
        """Modify an existing order"""
        if not self.platform_api:
            return -1
        return self.platform_api.place_modify_order(oms_order_id, new_price, new_qty)

    def cancel_order(self, oms_order_id: int) -> int:
        """Cancel an order"""
        if not self.platform_api:
            return -1
        return self.platform_api.place_cancel_order(oms_order_id)

    def log(self, message: str):
        """Log a message"""
        if self.platform_api:
            self.platform_api.log_msg(message)

    def send_status(self, traded_qty: int, achieved_spread: int, 
                   current_spread: int, is_complete: bool, has_opportunity: bool):
        """Send strategy status update"""
        if self.platform_api:
            self.platform_api.send_status_update(
                self.pf_id, traded_qty, achieved_spread,
                current_spread, is_complete, has_opportunity
            )

    @staticmethod
    def _order_to_leg(order: Order, event_time: int):
        """Convert Order to Leg for C++ layer"""
        # This will be implemented in the C wrapper
        pass

    def subscribe_token(self, token: int):
        """Subscribe to market data for token"""
        # Implemented in C wrapper
        pass

    def unsubscribe_token(self, token: int):
        """Unsubscribe from market data for token"""
        # Implemented in C wrapper
        pass

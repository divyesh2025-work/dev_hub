"""
Python ConRevIOC Strategy Example
Conversion/Reversal Immediate or Cancel strategy
"""

import json
from typing import Dict, Optional
from sdk.python_sdk import (
    StrategyAPI, MarketData, OrderUpdate, Order, OrderSide, OrderType, OrderState
)


class ConRevIOCParams:
    """Strategy parameters"""
    def __init__(self, data: bytes):
        # Parse from binary or dict (implementation depends on integration)
        self.fut_token: int = 0
        self.call_token: int = 0
        self.put_token: int = 0
        self.strike_price: int = 0
        self.max_lots: int = 0
        self.sol: int = 0  # Strike offset limit
        self.spread_threshold: int = 0
        self.is_conversion: bool = True


class ConRevIOCStrategy(StrategyAPI):
    """
    Conversion/Reversal IOC Strategy
    Trades futures, call, and put options to exploit arbitrage opportunities
    """

    def __init__(self):
        """Initialize strategy state"""
        super().__init__()
        self.params: ConRevIOCParams = None
        
        # State
        self.active: bool = False
        self.traded_qty: int = 0
        self.achieved_spread: int = 0
        self.legs_sent: bool = False
        
        # Market data cache
        self.fut_market: Optional[MarketData] = None
        self.call_market: Optional[MarketData] = None
        self.put_market: Optional[MarketData] = None
        
        # Pending orders
        self.fut_oms_id: int = 0
        self.call_oms_id: int = 0
        self.put_oms_id: int = 0
        
        # Parent tracking
        self.last_parent_oms_id: int = 0
        self.all_parent_oms_ids: list = []

    def on_add(self, params: dict) -> str:
        """Add portfolio and initialize parameters"""
        try:
            self.params = ConRevIOCParams(b'')  # Or parse from params
            
            # Parse parameters
            self.params.fut_token = params.get('fut_token', 0)
            self.params.call_token = params.get('call_token', 0)
            self.params.put_token = params.get('put_token', 0)
            self.params.strike_price = params.get('strike_price', 0)
            self.params.max_lots = params.get('max_lots', 0)
            self.params.sol = params.get('sol', 0)
            self.params.spread_threshold = params.get('spread_threshold', 0)
            self.params.is_conversion = params.get('is_conversion', True)
            
            # Initialize state
            self.active = False
            self.traded_qty = 0
            self.achieved_spread = 0
            self.legs_sent = False
            
            msg = (f"ADD: pf={self.pf_id} fut={self.params.fut_token} "
                   f"call={self.params.call_token} put={self.params.put_token} "
                   f"strike={self.params.strike_price} max={self.params.max_lots} "
                   f"spread={self.params.spread_threshold} conv={self.params.is_conversion}")
            self.log(msg)
            
            print("Here is the added call (Python)")
            return "Portfolio added"
        except Exception as e:
            return f"Error: {str(e)}"

    def on_edit(self, params: dict) -> str:
        """Edit portfolio parameters"""
        try:
            # Parse new params
            new_max_lots = params.get('max_lots', self.params.max_lots)
            new_spread_threshold = params.get('spread_threshold', self.params.spread_threshold)
            new_is_conversion = params.get('is_conversion', self.params.is_conversion)
            
            # Don't allow changing tokens/strike
            if (params.get('fut_token') != self.params.fut_token or
                params.get('call_token') != self.params.call_token or
                params.get('put_token') != self.params.put_token or
                params.get('strike_price') != self.params.strike_price):
                return "Error: Cannot modify tokens or strike"
            
            # Update runtime params
            self.params.max_lots = new_max_lots
            self.params.spread_threshold = new_spread_threshold
            self.params.is_conversion = new_is_conversion
            
            msg = (f"EDIT: pf={self.pf_id} max={self.params.max_lots} "
                   f"spread={self.params.spread_threshold} conv={self.params.is_conversion}")
            self.log(msg)
            
            return "Parameters updated"
        except Exception as e:
            return f"Error: {str(e)}"

    def on_run(self) -> str:
        """Start strategy and subscribe to market data"""
        try:
            if self.active:
                return "Error: Strategy already running"
            
            # Subscribe to tokens
            self.subscribe_token(self.params.fut_token)
            self.subscribe_token(self.params.call_token)
            self.subscribe_token(self.params.put_token)
            
            self.active = True
            self.legs_sent = False
            
            self.log("RUN: Strategy started")
            print("Here is the run call (Python)")
            return "Strategy running"
        except Exception as e:
            return f"Error: {str(e)}"

    def on_stop(self) -> str:
        print("Stopping strategy (Python)")
        """Stop strategy and cancel pending orders"""
        try:
            if not self.active:
                return "Error: Strategy not running"
            
            # Cancel pending orders
            if self.fut_oms_id:
                self.cancel_order(self.fut_oms_id)
            if self.call_oms_id:
                self.cancel_order(self.call_oms_id)
            if self.put_oms_id:
                self.cancel_order(self.put_oms_id)
            
            # Unsubscribe
            self.unsubscribe_token(self.params.fut_token)
            self.unsubscribe_token(self.params.call_token)
            self.unsubscribe_token(self.params.put_token)
            
            self.active = False
            
            self.log("STOP: Strategy stopped")
            return "Strategy stopped"
        except Exception as e:
            return f"Error: {str(e)}"

    def on_remove(self) -> str:
        """Remove portfolio and cleanup"""
        try:
            if self.active:
                self.on_stop()
            return "Portfolio removed"
        except Exception as e:
            return f"Error: {str(e)}"

    def on_query(self) -> dict:
        """Query strategy state"""
        return {
            'pf_id': self.pf_id,
            'active': self.active,
            'traded_qty': self.traded_qty,
            'max_lots': self.params.max_lots if self.params else 0,
            'achieved_spread': self.achieved_spread,
            'legs_sent': self.legs_sent
        }

    def on_market_event(self, market: MarketData):
        """Process market data and identify opportunities"""
        print(f"Received market data: token={market.token}  (PYTHON)")
        # self.log(f"MARKET EVENT: token={market.token} bid={market.bid()} ask={market.ask()}")
        if not self.active:
            return
        
        # Cache market data
        if market.token == self.params.fut_token:
            self.fut_market = market
        elif market.token == self.params.call_token:
            self.call_market = market
        elif market.token == self.params.put_token:
            self.put_market = market
        else:
            return
        
        # Check if we have all market data
        if not (self.fut_market and self.call_market and self.put_market):
            return
        
        # Only place orders if:
        # 1. Not already sent (IOC = one shot)
        # 2. Opportunity exists
        # 3. Not yet at max lots
        if self.legs_sent:
            return
        if self.traded_qty >= self.params.max_lots:
            return
        
        spread = self._compute_opportunity()
        if spread is None or spread < self.params.spread_threshold:
            return
        
        # Log opportunity
        self.log(f"OPPORTUNITY: spread={spread} (threshold={self.params.spread_threshold})")
        
        # Place all 3 legs
        qty = self.params.max_lots - self.traded_qty
        
        legs = self._create_legs(qty)
        ret = self.place_orders(legs, OrderType.IOC, market.event_time)
        
        if ret <= 0:
            self.log(f"Order placement failed: {ret}")
            return
        
        # Mark as sent
        self.legs_sent = True
        self.last_parent_oms_id = ret
        self.all_parent_oms_ids.append(ret)
        self.achieved_spread = spread

    def on_order_update(self, update: OrderUpdate):
        """Handle order updates"""
        if update.state == OrderState.FILL:
            self.traded_qty += update.filled_qty
            self.log(f"Fill: token={update.token} qty={update.filled_qty} price={update.avg_fill_price}")
            
            if self.traded_qty >= self.params.max_lots:
                self.log("Strategy complete - max lots reached")
        
        elif update.state == OrderState.PARTIAL_FILL:
            self.traded_qty += update.filled_qty
            self.log(f"PartialFill: token={update.token} filled={self.traded_qty}/{self.params.max_lots}")
        
        elif update.state in (OrderState.CANCEL_EXCHANGE, OrderState.EXCHANGE_REJECTED):
            self.log(f"Order cancelled/rejected: token={update.token} state={update.state}")

    # ══════════════════════════════════════════════════════════
    # HELPER METHODS
    # ══════════════════════════════════════════════════════════

    def _compute_opportunity(self) -> Optional[int]:
        print("Computing opportunity (Python)")
        """Compute spread and determine if opportunity exists"""
        if not all([self.fut_market, self.call_market, self.put_market]):
            return None
        
        fut_bid = self.fut_market.bid()
        fut_ask = self.fut_market.ask()
        call_bid = self.call_market.bid()
        call_ask = self.call_market.ask()
        put_bid = self.put_market.bid()
        put_ask = self.put_market.ask()
        
        if self.params.is_conversion:
            # CONVERSION: BUY UNDERLYING + SELL CALL + BUY PUT
            # Spread = (call_ask + put_bid + strike) - (fut_bid)
            spread = (call_ask + put_bid + self.params.strike_price) - fut_bid
        else:
            # REVERSAL: SELL UNDERLYING + BUY CALL + SELL PUT
            # Spread = fut_ask - (call_bid + put_ask + strike)
            spread = fut_ask - (call_bid + put_ask + self.params.strike_price)
        
        return spread

    def _create_legs(self, qty: int) -> list:
        """Create order legs based on conversion/reversal"""
        legs = []
        
        if self.params.is_conversion:
            # BUY FUTURES
            legs.append(Order(
                symbol_id=self.params.fut_token,
                price=self.fut_market.ask(),
                qty=qty,
                side=OrderSide.BUY
            ))
            # SELL CALL
            legs.append(Order(
                symbol_id=self.params.call_token,
                price=self.call_market.bid(),
                qty=qty,
                side=OrderSide.SELL
            ))
            # BUY PUT
            legs.append(Order(
                symbol_id=self.params.put_token,
                price=self.put_market.ask(),
                qty=qty,
                side=OrderSide.BUY
            ))
        else:
            # SELL FUTURES
            legs.append(Order(
                symbol_id=self.params.fut_token,
                price=self.fut_market.bid(),
                qty=qty,
                side=OrderSide.SELL
            ))
            # BUY CALL
            legs.append(Order(
                symbol_id=self.params.call_token,
                price=self.call_market.ask(),
                qty=qty,
                side=OrderSide.BUY
            ))
            # SELL PUT
            legs.append(Order(
                symbol_id=self.params.put_token,
                price=self.put_market.bid(),
                qty=qty,
                side=OrderSide.SELL
            ))
        
        return legs

#pragma once
#include "TemplateStrategy.h"
#include <OrderManagement/OrderManager.h>

#define MOD_MAX 10000000   // max modify attempts allowed
#define CANCEL_MAX 1000000 // max cancel attempts allowed

// Four Leg Bidding specialization with counter logic
template <>
struct StrategyExecutor<StrategyKind::BOX_1_1_1_1>
{
public:
    /**
     * @brief Main execution loop for the box bidding strategy.
     * @details Orchestrates the complete box spread trading lifecycle by managing state transitions,
     *          reading market data, validating prices, and delegating to appropriate state handlers.
     *          This function is the core orchestrator that runs once per market snapshot and controls
     *          the progression through all phases of a box spread trade from entry to completion.
     *
     * @param p Reference to the Portfolio containing strategy state, parameters, and execution data.
     * @param s Reference to the current StrategyMarketSnapshot with live market data for all legs.
     * @param o Reference to the OrderManager for submitting and modifying orders.
     * @param exe_time The execution timestamp in microseconds, used for time-based order management.
     *
     * @return bool Always returns true on successful execution. Returns false only if market data
     *         validation fails (i.e., any price equals zero).
     *
     * @details Function Flow:
     *   1. Initialize strategy parameters and extract current order state
     *   2. Capture current system timestamp for all time-based calculations
     *   3. Extract and cache all relevant market prices (bid/ask for all four legs)
     *   4. Validate that all prices are non-zero (market data integrity check)
     *   5. Display current market data for monitoring
     *   6. Execute state machine loop until completion:
     *      - IDLE: Evaluate new opportunities and initiate leg 1 orders
     *      - LEG1_PENDING: Monitor leg 1 fill status, handle timeouts/cancellations
     *      - LEG1_PARTIAL_FILLED: Manage partial fills and decide to continue or cancel
     *      - LEG1_FILLED: All of leg 1 is filled; prepare cover legs 2, 3, 4
     *      - LEGS234_PENDING: Monitor and manage cover leg order fills
     *      - COMPLETED: Check for additional lot opportunities before exiting
     *   7. Check if maximum order modification/cancellation limits have been reached
     *   8. Mark iteration complete if limits exceeded
     *
     * @note The state machine uses a while loop allowing multiple state transitions within
     *       a single run() call if handlers return false (continue loop).
     * @note All market prices are read only once at the beginning and cached in const variables
     *       for consistency and performance throughout the function.
     * @note State handlers are responsible for returning true (exit loop) or false (continue looping).
     *
     * @see BoxBiddingStates for state enumeration details
     * @see handleIdleState, handleLeg1PendingState, handleLeg1FilledState, handleLegs234PendingState,
     *      handleCompletedState for individual state handler documentation
     */
    static bool run(Portfolio &p,
                    const StrategyMarketSnapshot &s,
                    OrderManager &o, const unsigned long long &exe_time) noexcept
    {
        // ========== INITIALIZATION ==========
        auto &params = p.params.box_bidding;
        auto &order_data = p.order_data.box_bidding;

        // Log entry state for debugging and audit trails
        LOG_FILE("BoxBidding", "Starting bidding with state:" + std::to_string(static_cast<int>(order_data.state)));

        // Update strategy configuration flags from parameters
        order_data.is_flip = params.flip_box_enabled;
        order_data.entry_leg = params.entry_leg;

        LOG_FILE("BoxBidding", "Strategy Active");

        // ========== MARKET DATA CAPTURE ==========
        // Capture current timestamp once for consistency across all time-based operations
        const uint64_t current_time = getCurrentTimestamp();

        // Cache all relevant market prices to avoid multiple snapshot reads and ensure consistency
        // These prices are used throughout the state machine execution
        const uint32_t itm_call_ask = s.data.box_bidding.long_call.asks[0];  // Leg 1: Long call ask price
        const uint32_t itm_call_bid = s.data.box_bidding.long_call.bids[0];  // Leg 1: Long call bid price
        const uint32_t itm_put_ask = s.data.box_bidding.long_put.asks[0];    // Leg 4: Long put ask price
        const uint32_t itm_put_bid = s.data.box_bidding.long_put.bids[0];    // Leg 4: Long put bid price
        const uint32_t otm_call_ask = s.data.box_bidding.short_call.asks[0]; // Leg 3: Short call ask price
        const uint32_t otm_call_bid = s.data.box_bidding.short_call.bids[0]; // Leg 3: Short call bid price
        const uint32_t otm_put_ask = s.data.box_bidding.short_put.asks[0];   // Leg 2: Short put ask price
        const uint32_t otm_put_bid = s.data.box_bidding.short_put.bids[0];   // Leg 2: Short put bid price

        // Strike price difference defines the box intrinsic value at expiration
        const uint32_t strike_diff = order_data.strike_diff;

        // ========== DATA VALIDATION ==========
        // Ensure all market data is valid (no zero prices which indicate stale or missing data)
        // Using __builtin_expect for branch prediction optimization - false path is likely (valid prices)
        if (__builtin_expect(((!order_data.is_flip && itm_call_ask != 0 && itm_put_ask != 0 && otm_call_bid != 0 && otm_put_bid != 0) || (order_data.is_flip && itm_call_bid != 0 && itm_put_bid != 0 && otm_call_ask != 0 && otm_put_ask != 0)) == 0, 1))
        {
            LOG_COUT("builtin price check failed");
            return false; // Exit if any critical price is zero
        }

        // ========== STATE MACHINE EXECUTION ==========
        bool return_bool = false;
        while (!return_bool)
        {
            switch (order_data.state)
            {
            // State: IDLE - No active positions; evaluate new trading opportunities
            case BoxBiddingStates::IDLE:
            {
                // Handler returns true to exit loop if opportunity evaluation completes
                // Returns false to loop again if transitioning to next state within same cycle
                return_bool = handleIdleState(p, s, o, params, order_data, current_time,
                                              itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid,
                                              otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid,
                                              strike_diff, exe_time);
                break;
            }

            // State: LEG1_PENDING - Leg 1 order submitted; waiting for fills or timeout
            // Behavior: Do not loop multiple times; cancel order if stop requested
            case BoxBiddingStates::LEG1_PENDING:
            {
                // Handler monitors leg 1 fill status and order timeout management
                return_bool = handleLeg1PendingState(p, s, o, params, order_data, current_time,
                                                     itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid,
                                                     otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid,
                                                     strike_diff, exe_time);
                break;
            }

            // State: LEG1_PARTIAL_FILLED - Leg 1 has received partial fills
            // Behavior: Do not loop multiple times; cancel remaining on stop request
            case BoxBiddingStates::LEG1_PARTIAL_FILLED:
            {
                // Handler decides whether to accept partial fill and proceed to hedging
                // or cancel and restart
                return_bool = handleLeg1PartialFilledState(p, s, o, params, order_data, current_time,
                                                           itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid,
                                                           otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid,
                                                           strike_diff, exe_time);
                break;
            }

            // State: LEG1_FILLED - Leg 1 completely filled; ready to cover with legs 2, 3, 4
            // Behavior: Can transition to next state within same cycle
            case BoxBiddingStates::LEG1_FILLED:
            {
                // Handler prepares and submits cover leg orders (legs 2, 3, 4)
                // May loop back within same cycle if transitioning to cover state
                return_bool = handleLeg1FilledState(p, s, o, params, order_data, current_time,
                                                    itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid,
                                                    otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid,
                                                    strike_diff, exe_time);
                break;
            }

            // State: LEGS234_PENDING - cover legs (2, 3, 4) orders submitted; awaiting fills
            // Behavior: Can transition to next state within same cycle
            case BoxBiddingStates::LEGS234_PENDING:
            {
                // Handler monitors cover leg fill status and manages order lifecycle
                return_bool = handleLegs234PendingState(p, s, o, params, order_data, current_time,
                                                        itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid,
                                                        otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid,
                                                        strike_diff, exe_time);
                break;
            }

            // State: COMPLETED - Current box spread cycle finished; evaluate for next lot
            // Behavior: Exit loop; allows iteration in next cycle if more lots available
            case BoxBiddingStates::COMPLETED:
            {
                // Handler checks if additional lots can be executed based on max_lots parameter
                return_bool = handleCompletedState(p, s, o, params, order_data, current_time, exe_time);
                break;
            }

            // Fallback: Invalid state detected
            default:
            {
                LOG_FILE("BoxBidding", "Unknown state came");
                // Reset to IDLE state and exit loop for recovery in next cycle
                order_data.state = BoxBiddingStates::IDLE;
                return_bool = false;
            }
            }
        }

        // ========== COMPLETION PRINTS ==========
        LOG_FILE("BoxBidding", "Ending bidding with state:" + std::to_string(static_cast<int>(order_data.state)));

        return true;
    }

private:
    /**
     * @brief Retrieves the current system timestamp in microseconds.
     * @details Uses high-resolution clock to get current time and converts to microseconds
     *          since epoch. Used for timing order execution and state transitions.
     * @return uint64_t Current timestamp in microseconds since epoch.
     * @note This is an inline function optimized for performance in tight loops.
     */
    static uint64_t getCurrentTimestamp() noexcept
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    /**
     * @brief Calculates the total quantity covered across all cover legs (legs 2, 3, 4).
     * @details Returns the minimum quantity covered among legs 2, 3, and 4, representing
     *          the quantity that has protection across all positions in the box spread.
     * @param order_data The order data structure containing coverage quantities for each leg.
     * @return uint32_t Minimum covered quantity across legs 2, 3, and 4.
     * @note A quantity is only considered "covered" if ALL cover legs have covered it.
     */
    static uint32_t getTotalCoveredQty(const auto &order_data) noexcept
    {
        return std::min({order_data.leg2_covered_qty, order_data.leg3_covered_qty, order_data.leg4_covered_qty});
    }

    /**
     * @brief Calculates the quantity that is filled in leg 1 but not yet covered by cover legs.
     * @details Computes uncovered quantity as the difference between leg 1 fills and total coverage.
     *          Returns 0 if leg 1 is fully covered by the cover legs.
     * @param order_data The order data structure containing leg 1 fill and coverage quantities.
     * @return uint32_t Quantity of leg 1 that lacks cover leg coverage.
     * @note This represents risk exposure that requires immediate hedging via legs 2, 3, 4.
     */
    static uint32_t getUncoveredQty(const auto &order_data) noexcept
    {
        const uint32_t total_covered = getTotalCoveredQty(order_data);
        return order_data.leg1_filled_qty > total_covered ? order_data.leg1_filled_qty - total_covered : 0;
    }

    /**
     * @brief Calculates the total completed quantity of the entire box spread trade.
     * @details Returns the minimum of all four leg fill quantities. A complete trade requires
     *          all legs to be filled proportionally, so the limiting leg determines total filled.
     * @param order_data The order data structure containing actual fill quantities for each leg.
     * @return uint32_t Total quantity completed across all four legs of the box spread.
     * @note Used to determine strategy completion and profitability realization.
     */
    static uint32_t getTotalTradedQty(auto &order_data)
    {
        return std::min({order_data.leg1_filled_qty, order_data.leg2_actual_fill_qty, order_data.leg3_actual_fill_qty, order_data.leg4_actual_fill_qty});
    }

    /**
     * @brief Calculates the total quantity that has been completely filled across all legs.
     * @details Identical to getTotalTradedQty - returns the minimum fill quantity across
     *          all four legs, representing the most conservative measure of completion.
     * @param order_data The order data structure containing actual fill quantities for each leg.
     * @return uint32_t Total quantity with fills confirmed on all four legs.
     * @note This is a redundant function with getTotalTradedQty; consider consolidation.
     */
    static uint32_t getTotalCoveredFilledQty(const auto &order_data) noexcept
    {
        return std::min({order_data.leg1_filled_qty, order_data.leg2_actual_fill_qty, order_data.leg3_actual_fill_qty, order_data.leg4_actual_fill_qty});
    }

    /**
     * @brief Calculates leg 2 (short put) quantity that has not yet been covered.
     * @details Computes the difference between leg 1 fills and leg 2 coverage.
     *          Represents the notional amount exposed on the short put leg.
     * @param order_data The order data structure containing leg 1 fills and leg 2 coverage.
     * @return uint32_t Quantity of leg 2 exposure lacking coverage; 0 if fully covered.
     * @note Leg 2 is the short put position in the box spread structure.
     */
    static uint32_t getLeg2UncoveredQty(const auto &order_data) noexcept
    {
        // Debug logging for coverage tracking
        return order_data.leg1_filled_qty > order_data.leg2_covered_qty ? order_data.leg1_filled_qty - order_data.leg2_covered_qty : 0;
    }

    /**
     * @brief Calculates leg 3 (short call) quantity that has not yet been covered.
     * @details Computes the difference between leg 1 fills and leg 3 coverage.
     *          Represents the notional amount exposed on the short call leg.
     * @param order_data The order data structure containing leg 1 fills and leg 3 coverage.
     * @return uint32_t Quantity of leg 3 exposure lacking coverage; 0 if fully covered.
     * @note Leg 3 is the short call position in the box spread structure.
     */
    static uint32_t getLeg3UncoveredQty(const auto &order_data) noexcept
    {
        return order_data.leg1_filled_qty > order_data.leg3_covered_qty ? order_data.leg1_filled_qty - order_data.leg3_covered_qty : 0;
    }

    /**
     * @brief Calculates leg 4 (long call) quantity that has not yet been covered.
     * @details Computes the difference between leg 1 fills and leg 4 coverage.
     *          Represents the notional amount exposed on the long call leg.
     * @param order_data The order data structure containing leg 1 fills and leg 4 coverage.
     * @return uint32_t Quantity of leg 4 exposure lacking coverage; 0 if fully covered.
     * @note Leg 4 is the long call position in the box spread structure.
     */
    static uint32_t getLeg4UncoveredQty(const auto &order_data) noexcept
    {
        return order_data.leg1_filled_qty > order_data.leg4_covered_qty ? order_data.leg1_filled_qty - order_data.leg4_covered_qty : 0;
    }

    /**
     * @brief Evaluates whether current market prices present a profitable box spread opportunity.
     * @details Calculates the net spread cost by considering all four leg prices and comparing
     *          against the configured spread threshold. Supports both normal and flip strategies.
     *          In flip mode, the strategy reverses the spread calculation (sells the box).
     *          In normal mode, the strategy buys the box spread.
     *
     * @param p The portfolio containing strategy parameters and configuration.
     * @param itm_call_ask Ask price for the in-the-money call (leg 1 - long call).
     * @param itm_call_bid Bid price for the in-the-money call.
     * @param itm_put_ask Ask price for the in-the-money put (long put).
     * @param itm_put_bid Bid price for the in-the-money put.
     * @param otm_call_ask Ask price for the out-of-the-money call (short call).
     * @param otm_call_bid Bid price for the out-of-the-money call.
     * @param otm_put_ask Ask price for the out-of-the-money put (short put).
     * @param otm_put_bid Bid price for the out-of-the-money put.
     * @param strike_diff Difference between strike prices (defines the box value at expiration).
     * @param is_flip Boolean flag indicating if the strategy should execute as a flip (reverse box).
     * @param params Strategy parameters containing spread thresholds and configuration.
     * @param spread Output parameter that will contain the calculated spread value.
     *
     * @return bool True if current_spread >= threshold (profitable opportunity exists); false otherwise.
     * @return bool Always returns true in DEBUG mode to allow testing without market restrictions.
     *
     * @details Spread Calculation:
     *   - Normal Mode (buy box): strike_diff - itm_call_ask - itm_put_ask + otm_put_bid + otm_call_bid
     *   - Flip Mode (sell box): -strike_diff + itm_call_bid + itm_put_bid - otm_put_ask - otm_call_ask
     *
     * @note The threshold is configurable via params.leg1_spread_threshold and represents
     *       the minimum acceptable profit margin before considering the trade worthwhile.
     * @note This function uses built-in compiler optimizations for fast price calculations.
     */
    /**
     * @brief Evaluates whether current market prices present a profitable box spread opportunity.
     * @details Calculates the net spread cost by considering all four leg prices and comparing
     *          against the configured spread threshold. Supports both normal and flip strategies.
     *          In flip mode, the strategy reverses the spread calculation (sells the box).
     *          In normal mode, the strategy buys the box spread.
     *
     * @param p The portfolio containing strategy parameters and configuration.
     * @param itm_call_ask Ask price for the in-the-money call (leg 1 - long call).
     * @param itm_call_bid Bid price for the in-the-money call.
     * @param itm_put_ask Ask price for the in-the-money put (long put).
     * @param itm_put_bid Bid price for the in-the-money put.
     * @param otm_call_ask Ask price for the out-of-the-money call (short call).
     * @param otm_call_bid Bid price for the out-of-the-money call.
     * @param otm_put_ask Ask price for the out-of-the-money put (short put).
     * @param otm_put_bid Bid price for the out-of-the-money put.
     * @param strike_diff Difference between strike prices (defines the box value at expiration).
     * @param is_flip Boolean flag indicating if the strategy should execute as a flip (reverse box).
     * @param params Strategy parameters containing spread thresholds and configuration.
     * @param spread Output parameter that will contain the calculated spread value.
     *
     * @return bool True if current_spread >= threshold (profitable opportunity exists); false otherwise.
     * @return bool Always returns true in DEBUG mode to allow testing without market restrictions.
     *
     * @details Spread Calculation:
     *   - Normal Mode (buy box): strike_diff - itm_call_ask - itm_put_ask + otm_put_bid + otm_call_bid
     *   - Flip Mode (sell box): -strike_diff + itm_call_bid + itm_put_bid - otm_put_ask - otm_call_ask
     *
     * @note The threshold is configurable via params.leg1_spread_threshold and represents
     *       the minimum acceptable profit margin before considering the trade worthwhile.
     * @note This function uses built-in compiler optimizations for fast price calculations.
     */
    static bool checkSpreadOpportunity(const Portfolio &p,
                                       uint32_t itm_call_ask, uint32_t itm_call_bid,
                                       uint32_t itm_put_ask, uint32_t itm_put_bid,
                                       uint32_t otm_call_ask, uint32_t otm_call_bid,
                                       uint32_t otm_put_ask, uint32_t otm_put_bid,
                                       uint32_t strike_diff, bool is_flip, const auto &params,
                                       uint32_t &spread) noexcept
    {
#ifdef DEBUG
        // In debug mode, always return true to allow testing without market restrictions
        return true;
#endif

        int64_t current_spread;
        int64_t threshold = params.leg1_spread_threshold;

        // LOG_COUT("FLIP : " + std::to_string(is_flip));
        // LOG_COUT("leg1SpreadThreshold: " << params.leg1_spread_threshold);

        if (is_flip)
        {
            // Flip mode: sell the box - reverse the profit calculation
            // Short higher strike call, short lower strike put, long lower strike call, long higher strike put
            current_spread = -int64_t(strike_diff) + int64_t(itm_call_bid) + int64_t(itm_put_bid) - int64_t(otm_put_ask) - int64_t(otm_call_ask);
        }
        else
        {
            // Normal mode: buy the box - standard profit calculation
            // Long higher strike call, long lower strike put, short lower strike call, short higher strike put
            // LOG_COUT(std::to_string(strike_diff) + "-" + std::to_string(itm_call_ask) + "-" + std::to_string(itm_put_ask) + "+" + std::to_string(otm_put_bid) + "+" + std::to_string(otm_call_bid));
            current_spread = int64_t(strike_diff) - int64_t(itm_call_ask) - int64_t(itm_put_ask) + int64_t(otm_put_bid) + int64_t(otm_call_bid);
        }

        // Store calculated spread for external use and logging
        spread = current_spread;

        // LOG_COUT("current_spread: " + std::to_string(current_spread));
        // LOG_COUT("threshold: " + std::to_string(threshold));

        // Opportunity exists if spread meets or exceeds the profitability threshold
        return current_spread >= threshold;
    }

    /**
     * @brief Calculates the optimal bidding price for leg 1 of the box spread based on market conditions.
     * @details Determines the entry price for the initial leg depending on which leg is being used as entry point,
     *          market prices, strike difference, and whether the strategy is in normal (buy box) or flip (sell box) mode.
     *          The function applies price adjustments via strategy parameters and validates that the calculated
     *          price remains positive. In DEBUG mode, returns a static test price for testing purposes.
     *
     * @param itm_call_ask Ask price for the in-the-money call (long call).
     * @param itm_call_bid Bid price for the in-the-money call.
     * @param itm_put_ask Ask price for the in-the-money put (long put).
     * @param itm_put_bid Bid price for the in-the-money put.
     * @param otm_call_ask Ask price for the out-of-the-money call (short call).
     * @param otm_call_bid Bid price for the out-of-the-money call.
     * @param otm_put_ask Ask price for the out-of-the-money put (short put).
     * @param otm_put_bid Bid price for the out-of-the-money put.
     * @param strike_diff The difference between strike prices (intrinsic value at expiration).
     * @param order_data Reference to order data containing leg1 selection and is_flip flag.
     * @param params Reference to strategy parameters containing price adjustments.
     *
     * @return uint32_t The calculated bidding price for leg 1. Returns 0 if calculated price is negative or invalid.
     *
     * @details Price Calculations by Entry Leg:
     *   - ITM_CALL (Leg 1 entry):
     *     * Flip: strike_diff - itm_put_bid + flip_price_difference + otm_put_ask + otm_call_ask
     *     * Normal: strike_diff - price_difference - itm_put_ask + otm_put_bid + otm_call_bid
     *   - OTM_PUT (Leg 1 entry):
     *     * Flip: -strike_diff + itm_put_bid - flip_price_difference + itm_put_bid - otm_call_ask
     *     * Normal: -strike_diff + price_difference + itm_put_ask + itm_call_ask - otm_call_bid
     *   - OTM_CALL (Leg 1 entry):
     *     * Flip: -strike_diff + itm_put_bid - flip_price_difference - otm_put_ask + itm_call_bid
     *     * Normal: -strike_diff + price_difference + itm_put_ask - otm_put_bid + itm_call_ask
     *   - ITM_PUT (Leg 1 entry):
     *     * Flip: strike_diff - itm_call_bid + flip_price_difference + otm_put_ask + otm_call_ask
     *     * Normal: strike_diff - price_difference - itm_call_ask + otm_put_bid + otm_call_bid
     *
     * @note The choice of entry leg determines which market prices are weighted in the calculation,
     *       affecting the execution strategy and risk profile.
     * @note Price adjustments (price_difference or flip_price_difference) allow for strategy customization
     *       to maintain desired profit margins or spread characteristics.
     * @note DEBUG mode override allows testing without market data constraints.
     */
    static uint32_t calculateLeg1Price(uint32_t itm_call_ask, uint32_t itm_call_bid,
                                       uint32_t itm_put_ask, uint32_t itm_put_bid,
                                       uint32_t otm_call_ask, uint32_t otm_call_bid,
                                       uint32_t otm_put_ask, uint32_t otm_put_bid,
                                       uint32_t strike_diff, auto &order_data, auto &params) noexcept
    {
        int64_t price;
        bool is_flip = order_data.is_flip;

        switch (order_data.leg1)
        {
        // ========== ITM_CALL ENTRY POINT ==========
        case BoxBiddingLegs::ITM_CALL:
        {
            LOG_FILE("BoxBidding", "Leg1: ITM_CALL");

#ifdef DEBUG
            // Return static test price in debug mode for testing
            return TEST_PRICE_LEG1;
#endif

            if (is_flip)
            {
                // Flip mode calculation for selling the box via ITM call entry
                LOG_FILE("BoxBidding", "Flip Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", itm_put_bid=" + std::to_string(itm_put_bid) +
                                           ", flip_price_difference=" + std::to_string(params.flip_price_difference) +
                                           ", otm_put_ask=" + std::to_string(otm_put_ask) +
                                           ", otm_call_ask=" + std::to_string(otm_call_ask));

                price = int64_t(strike_diff) - int64_t(itm_put_bid) + int64_t(params.flip_price_difference) +
                        int64_t(otm_put_ask) + int64_t(otm_call_ask);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            else
            {
                // Normal mode calculation for buying the box via ITM call entry
                LOG_FILE("BoxBidding", "Normal Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", price_difference=" + std::to_string(params.price_difference) +
                                           ", itm_put_ask=" + std::to_string(itm_put_ask) +
                                           ", otm_put_bid=" + std::to_string(otm_put_bid) +
                                           ", otm_call_bid=" + std::to_string(otm_call_bid));

                price = int64_t(strike_diff) - int64_t(params.price_difference) - int64_t(itm_put_ask) +
                        int64_t(otm_put_bid) + int64_t(otm_call_bid);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            break;
        }

        // ========== OTM_PUT ENTRY POINT ==========
        case BoxBiddingLegs::OTM_PUT:
        {
            LOG_FILE("BoxBidding", "Leg1: OTM_PUT");

#ifdef DEBUG
            // Return static test price in debug mode for testing
            return TEST_PRICE_LEG2;
#endif

            if (is_flip)
            {
                // Flip mode calculation for selling the box via OTM put entry
                LOG_FILE("BoxBidding", "Flip Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", itm_put_bid=" + std::to_string(itm_put_bid) +
                                           ", flip_price_difference=" + std::to_string(params.flip_price_difference) +
                                           ", otm_call_ask=" + std::to_string(otm_call_ask));

                price = -int64_t(strike_diff) + int64_t(itm_put_bid) -
                        int64_t(params.flip_price_difference) + int64_t(itm_put_bid) -
                        int64_t(otm_call_ask);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            else
            {
                // Normal mode calculation for buying the box via OTM put entry
                LOG_FILE("BoxBidding", "Normal Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", price_difference=" + std::to_string(params.price_difference) +
                                           ", itm_put_ask=" + std::to_string(itm_put_ask) +
                                           ", itm_call_ask=" + std::to_string(itm_call_ask) +
                                           ", otm_call_bid=" + std::to_string(otm_call_bid));

                price = -int64_t(strike_diff) + int64_t(params.price_difference) + int64_t(itm_put_ask) +
                        int64_t(itm_call_ask) - int64_t(otm_call_bid);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            break;
        }

        // ========== OTM_CALL ENTRY POINT ==========
        case BoxBiddingLegs::OTM_CALL:
        {
            LOG_FILE("BoxBidding", "Leg1: OTM_CALL");

#ifdef DEBUG
            // Return static test price in debug mode for testing
            return TEST_PRICE_LEG3;
#endif

            if (is_flip)
            {
                // Flip mode calculation for selling the box via OTM call entry
                LOG_FILE("BoxBidding", "Flip Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", itm_put_bid=" + std::to_string(itm_put_bid) +
                                           ", flip_price_difference=" + std::to_string(params.flip_price_difference) +
                                           ", otm_put_ask=" + std::to_string(otm_put_ask) +
                                           ", itm_call_bid=" + std::to_string(itm_call_bid));

                price = -int64_t(strike_diff) + int64_t(itm_put_bid) -
                        int64_t(params.flip_price_difference) - int64_t(otm_put_ask) +
                        int64_t(itm_call_bid);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            else
            {
                // Normal mode calculation for buying the box via OTM call entry
                LOG_FILE("BoxBidding", "Normal Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", price_difference=" + std::to_string(params.price_difference) +
                                           ", itm_put_ask=" + std::to_string(itm_put_ask) +
                                           ", otm_put_bid=" + std::to_string(otm_put_bid) +
                                           ", itm_call_ask=" + std::to_string(itm_call_ask));

                price = -int64_t(strike_diff) + int64_t(params.price_difference) + int64_t(itm_put_ask) -
                        int64_t(otm_put_bid) + int64_t(itm_call_ask);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            break;
        }

        // ========== ITM_PUT ENTRY POINT ==========
        case BoxBiddingLegs::ITM_PUT:
        {
            LOG_FILE("BoxBidding", "Leg1: ITM_PUT");

#ifdef DEBUG
            // Return static test price in debug mode for testing
            return TEST_PRICE_LEG4;
#endif

            if (is_flip)
            {
                // Flip mode calculation for selling the box via ITM put entry
                LOG_FILE("BoxBidding", "Flip Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", itm_call_bid=" + std::to_string(itm_call_bid) +
                                           ", flip_price_difference=" + std::to_string(params.flip_price_difference) +
                                           ", otm_put_ask=" + std::to_string(otm_put_ask) +
                                           ", otm_call_ask=" + std::to_string(otm_call_ask));

                price = int64_t(strike_diff) - int64_t(itm_call_bid) +
                        int64_t(params.flip_price_difference) + int64_t(otm_put_ask) +
                        int64_t(otm_call_ask);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            else
            {
                // Normal mode calculation for buying the box via ITM put entry
                LOG_FILE("BoxBidding", "Normal Price Calculation");

                LOG_FILE("BoxBidding", "Values: strike_diff=" + std::to_string(strike_diff) +
                                           ", price_difference=" + std::to_string(params.price_difference) +
                                           ", itm_call_ask=" + std::to_string(itm_call_ask) +
                                           ", otm_put_bid=" + std::to_string(otm_put_bid) +
                                           ", otm_call_bid=" + std::to_string(otm_call_bid));

                price = int64_t(strike_diff) - int64_t(params.price_difference) -
                        int64_t(itm_call_ask) + int64_t(otm_put_bid) +
                        int64_t(otm_call_bid);

                LOG_FILE("BoxBidding", "Calculated price: " + std::to_string(price));
                return price > 0 ? uint32_t(price) : 0;
            }
            break;
        }

        // ========== INVALID LEG ==========
        default:
        {
            LOG_FILE("BoxBidding", "Invalid box info");
            return 0;
        }
        }
    }

    /**
     * @brief Retrieves the market price for non-bidding legs of the box spread.
     * @details For legs that are not being actively bid on, this function returns the appropriate
     *          market price (bid or ask) based on the leg type and strategy mode. Non-bidding legs
     *          use passive execution strategy, taking available liquidity at current market prices
     *          rather than aggressive bidding/offering. The price selection depends on whether the
     *          strategy is in normal mode (buy box) or flip mode (sell box).
     *
     * @param leg_info Reference to BoxBiddingLegs enumeration specifying which leg to get price for.
     * @param is_flip Boolean flag indicating strategy mode:
     *                - true: Flip mode (sell the box spread)
     *                - false: Normal mode (buy the box spread)
     * @param itm_call_ask Ask price for the in-the-money call (long call).
     * @param itm_call_bid Bid price for the in-the-money call.
     * @param itm_put_ask Ask price for the in-the-money put (long put).
     * @param itm_put_bid Bid price for the in-the-money put.
     * @param otm_call_ask Ask price for the out-of-the-money call (short call).
     * @param otm_call_bid Bid price for the out-of-the-money call.
     * @param otm_put_ask Ask price for the out-of-the-money put (short put).
     * @param otm_put_bid Bid price for the out-of-the-money put.
     *
     * @return uint64_t The selected market price for the specified leg. Returns 0 for invalid leg input.
     *
     * @details Price Selection Logic by Leg:
     *   - ITM_CALL: Flip mode uses bid (selling), normal mode uses ask (buying)
     *   - OTM_PUT: Flip mode uses ask (buying), normal mode uses bid (selling)
     *   - OTM_CALL: Flip mode uses ask (buying), normal mode uses bid (selling)
     *   - ITM_PUT: Flip mode uses bid (selling), normal mode uses ask (buying)
     *
     * @note Non-bidding legs accept passive execution; they do not post new bids/offers but take
     *       existing market liquidity at the prevailing bid or ask price.
     * @note The bid/ask selection ensures the strategy always takes the most advantageous available price
     *       for each leg based on whether it is being bought or sold in the current strategy mode.
     * @note This function should only be called for legs 2, 3, and 4 (the cover legs after leg 1 entry).
     */
    static uint32_t getNonBiddingLegsPrice(BoxBiddingLegs &leg_info, bool &is_flip,
                                           uint32_t itm_call_ask, uint32_t itm_call_bid,
                                           uint32_t itm_put_ask, uint32_t itm_put_bid,
                                           uint32_t otm_call_ask, uint32_t otm_call_bid,
                                           uint32_t otm_put_ask, uint32_t otm_put_bid)
    {
        switch (leg_info)
        {
        // ========== ITM_CALL LEG PRICING ==========
        case BoxBiddingLegs::ITM_CALL:
        {
            // Long call position: use bid in flip (selling), ask in normal (buying)
            if (is_flip)
            {
                // Flip mode: selling long call, use bid price
                return itm_call_bid;
            }
            else
            {
                // Normal mode: buying long call, use ask price
                return itm_call_ask;
            }
        }

        // ========== OTM_PUT LEG PRICING ==========
        case BoxBiddingLegs::OTM_PUT:
        {
            // Short put position: use ask in flip (buying), bid in normal (selling)
            if (is_flip)
            {
                // Flip mode: buying short put (covering), use ask price
                return otm_put_ask;
            }
            else
            {
                // Normal mode: selling short put, use bid price
                return otm_put_bid;
            }
        }

        // ========== OTM_CALL LEG PRICING ==========
        case BoxBiddingLegs::OTM_CALL:
        {
            // Short call position: use ask in flip (buying), bid in normal (selling)
            if (is_flip)
            {
                // Flip mode: buying short call (covering), use ask price
                return otm_call_ask;
            }
            else
            {
                // Normal mode: selling short call, use bid price
                return otm_call_bid;
            }
        }

        // ========== ITM_PUT LEG PRICING ==========
        case BoxBiddingLegs::ITM_PUT:
        {
            // Long put position: use bid in flip (selling), ask in normal (buying)
            if (is_flip)
            {
                // Flip mode: selling long put, use bid price
                return itm_put_bid;
            }
            else
            {
                // Normal mode: buying long put, use ask price
                return itm_put_ask;
            }
        }

        // ========== INVALID LEG ==========
        default:
        {
            LOG_FILE("BoxBidding", "Wrong leg info for box");
            return 0;
        }
        }
    }

    /**
     * @brief Determines the buy/sell side for a given leg based on strategy mode and leg type.
     * @details Maps each leg of the box spread to its corresponding trading side (buy or sell)
     *          based on the strategy mode and position type. This ensures that legs are executed
     *          on the correct side of the market: buying long positions and selling short positions
     *          in normal mode, with the sides reversed in flip mode (selling longs, buying shorts).
     *
     * @param leg_info Reference to BoxBiddingLegs enumeration specifying which leg to determine side for.
     * @param is_flip Boolean flag indicating strategy mode:
     *                - true: Flip mode (sell the box - reverse standard sides)
     *                - false: Normal mode (buy the box - standard box structure)
     *
     * @return Side The order side (Buy or Sell) for the specified leg in the current strategy mode.
     *              Returns Side::Buy as fallback for invalid input.
     *
     * @details Side Determination by Leg and Mode:
     *   - ITM_CALL (long call): Buy in normal mode, Sell in flip mode
     *   - OTM_PUT (short put): Sell in normal mode, Buy in flip mode
     *   - OTM_CALL (short call): Sell in normal mode, Buy in flip mode
     *   - ITM_PUT (long put): Buy in normal mode, Sell in flip mode
     *
     * @note Normal box spread structure (buy box):
     *       - Buy ITM call + Buy ITM put (long positions)
     *       - Sell OTM call + Sell OTM put (short positions)
     *
     * @note Flip box spread structure (sell box):
     *       - Sell ITM call + Sell ITM put (reverse long positions to sells)
     *       - Buy OTM call + Buy OTM put (reverse short positions to buys)
     *
     * @note This function is used during order submission to ensure correct market-facing
     *       buy/sell directives for each leg of the spread.
     */
    static Side ALWAYS_INLINE BuySell(BoxBiddingLegs &leg_info, bool &is_flip)
    {
        switch (leg_info)
        {
        // ========== ITM_CALL LEG SIDE ==========
        case BoxBiddingLegs::ITM_CALL:
        {
            // Long call: Buy in normal mode, Sell in flip mode
            if (is_flip)
            {
                return Side::Sell; // Flip mode: sell ITM call
            }
            else
            {
                return Side::Buy; // Normal mode: buy ITM call
            }
        }

        // ========== OTM_PUT LEG SIDE ==========
        case BoxBiddingLegs::OTM_PUT:
        {
            // Short put: Sell in normal mode, Buy in flip mode
            if (is_flip)
            {
                return Side::Buy; // Flip mode: buy OTM put
            }
            else
            {
                return Side::Sell; // Normal mode: sell OTM put
            }
        }

        // ========== OTM_CALL LEG SIDE ==========
        case BoxBiddingLegs::OTM_CALL:
        {
            // Short call: Sell in normal mode, Buy in flip mode
            if (is_flip)
            {
                return Side::Buy; // Flip mode: buy OTM call
            }
            else
            {
                return Side::Sell; // Normal mode: sell OTM call
            }
        }

        // ========== ITM_PUT LEG SIDE ==========
        case BoxBiddingLegs::ITM_PUT:
        {
            // Long put: Buy in normal mode, Sell in flip mode
            if (is_flip)
            {
                return Side::Sell; // Flip mode: sell ITM put
            }
            else
            {
                return Side::Buy; // Normal mode: buy ITM put
            }
        }

        // ========== INVALID LEG ==========
        default:
        {
            LOG_FILE("BoxBidding", "Wrong leg info for box");
            return Side::Buy; // Fallback to Buy for safety
        }
        }
    }

    /**
     * @brief Monitors and modifies leg 2 (short put) orders based on current market prices and fill status.
     * @details Implements a depth-based price tracking strategy for leg 2. Uses a counter mechanism to
     *          periodically escalate from best bid/ask (depth 0) to second-best prices (depth 1) to improve
     *          fill rates when orders are not filling. Modifies orders when market prices change or when
     *          the total quantity (pending + filled) diverges from tracked quantity. Revalidates depth
     *          by falling back to best prices if second-level quotes become unavailable.
     *
     * @param p Reference to Portfolio containing leg symbols and portfolio ID.
     * @param s Reference to StrategyMarketSnapshot (unused but kept for interface consistency).
     * @param o Reference to OrderManager for sending order modifications.
     * @param params Reference to strategy parameters (unused but kept for interface consistency).
     * @param order_data Reference to order tracking data containing leg 2 state, quantities, and prices.
     * @param modified Reference to boolean flag; set to true if order modification was sent successfully.
     * @param exe_time Execution timestamp passed to order manager for order timing.
     *
     * @return void This function modifies order_data in-place and updates the modified flag.
     *
     * @details Execution Flow:
     *   1. Increment leg 2 counter; if counter reaches 4, escalate to depth 1 and reset counter
     *   2. Retrieve current leg 2 price from best (depth 0) or second-best (depth 1) market levels
     *   3. If depth 1 price is zero, fallback to depth 0 as safety measure
     *   4. Check if order modification is needed:
     *      - Price has changed from last tracked price, OR
     *      - Total quantity (pending + filled) differs from last tracked quantity
     *   5. If modification needed and pending ACK available, construct new Leg order with:
     *      - Updated price
     *      - Cumulative quantity (leg2_filled_qty + leg2_pending_qty)
     *      - Correct buy/sell side based on strategy mode
     *   6. Send modification through OrderManager; set modified flag and clear ACK on success
     *
     * @note Depth escalation: counter reaches 4 every 4 market ticks, allowing aggressive pursuit
     *       of fills by moving to less competitive price levels.
     * @note The ACK flag (leg2_ack) acts as a semaphore to prevent rapid successive modifications
     *       while awaiting acknowledgment from the order management system.
     * @note Quantity tracking uses separate pending/filled quantities to handle partial fills correctly.
     */
    static void handleLeg2Modifications(Portfolio &p,
                                        const StrategyMarketSnapshot &s,
                                        OrderManager &o,
                                        const auto &params,
                                        auto &order_data, bool &modified, const unsigned long long &exe_time) noexcept
    {
        LOG_FILE("BoxBidding", "Enetered 2nd Modification");

        // ========== DEPTH ESCALATION COUNTER ==========
        // Mark that market data has been processed this tick
        p.updated_tick = false;

        // Increment counter for depth escalation logic
        order_data.leg2_counter++;

        // After 4 ticks, escalate to second-level prices and reset counter
        if (order_data.leg2_counter >= 4)
        {
            order_data.leg2_depth = 1;
            order_data.leg2_counter = 0;
        }

        // ========== PRICE RETRIEVAL ==========
        // Select price from best (depth 0) or second-best (depth 1) market levels
        uint32_t current_leg2_price;

        if (order_data.leg2_depth == 0)
        {
            // Use best bid/ask (top of book)
            current_leg2_price = getNonBiddingLegsPrice(order_data.leg2, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[0], order_data.snap.data.box_bidding.long_call.bids[0],
                                                        order_data.snap.data.box_bidding.long_put.asks[0], order_data.snap.data.box_bidding.long_put.bids[0],
                                                        order_data.snap.data.box_bidding.short_call.asks[0], order_data.snap.data.box_bidding.short_call.bids[0],
                                                        order_data.snap.data.box_bidding.short_put.asks[0], order_data.snap.data.box_bidding.short_put.bids[0]);
        }
        else
        {
            // Use second-level bid/ask (escalated level for aggressive fill pursuit)
            current_leg2_price = getNonBiddingLegsPrice(order_data.leg2, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[1], order_data.snap.data.box_bidding.long_call.bids[1],
                                                        order_data.snap.data.box_bidding.long_put.asks[1], order_data.snap.data.box_bidding.long_put.bids[1],
                                                        order_data.snap.data.box_bidding.short_call.asks[1], order_data.snap.data.box_bidding.short_call.bids[1],
                                                        order_data.snap.data.box_bidding.short_put.asks[1], order_data.snap.data.box_bidding.short_put.bids[1]);
        }

        // ========== DEPTH FALLBACK SAFETY CHECK ==========
        // If second-level quote is unavailable (zero), revert to best level
        if (current_leg2_price == 0 && order_data.leg2_depth == 1)
        {
            LOG_COUT("In a suspected wrong section of Leg 2 Modifications");
            order_data.leg2_depth = 0;
            current_leg2_price = getNonBiddingLegsPrice(order_data.leg2, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[0], order_data.snap.data.box_bidding.long_call.bids[0],
                                                        order_data.snap.data.box_bidding.long_put.asks[0], order_data.snap.data.box_bidding.long_put.bids[0],
                                                        order_data.snap.data.box_bidding.short_call.asks[0], order_data.snap.data.box_bidding.short_call.bids[0],
                                                        order_data.snap.data.box_bidding.short_put.asks[0], order_data.snap.data.box_bidding.short_put.bids[0]);
        }

        // ========== MODIFICATION TRIGGER CHECK ==========
        LOG_FILE("BoxBidding", "current_leg2_price:" + std::to_string(current_leg2_price) + ",order_data.last_leg2_price:" + std::to_string(order_data.last_leg2_price) + ",order_data.leg2_pending_qty :" + std::to_string(order_data.leg2_pending_qty) + ",order_data.last_leg2_qty:" + std::to_string(order_data.last_leg2_qty) + " oms id for this is :" + std::to_string(order_data.leg2_order_id));

        // Modify if: (1) price changed, OR (2) total quantity (pending + filled) changed
        if (current_leg2_price > 0 &&
            (current_leg2_price != order_data.last_leg2_price || order_data.leg2_pending_qty + order_data.leg2_filled_qty != order_data.last_leg2_qty))
        {
            // ========== ORDER MODIFICATION ==========
            Leg leg2;
            uint32_t new_order_qty = order_data.leg2_filled_qty + order_data.leg2_pending_qty;

            // Construct modified leg with updated price and cumulative quantity
            leg2 = {p.legs[1].symbol_token, current_leg2_price, new_order_qty, BuySell(order_data.leg2, order_data.is_flip), 0, order_data.leg2_order_id};

            // Send modification; update tracking on success
            if (order_data.leg2_ack && o.sendModifyPlacement(p.portfolio_id, order_data.leg2_order_id, leg2, exe_time, 2))
            {
                order_data.leg2_ack = false; // Clear ACK flag, wait for new acknowledgment
                modified = true;             // Signal that modification was sent
            }
        }
    }

    /**
     * @brief Monitors and modifies leg 3 (short call) orders based on current market prices and fill status.
     * @details Implements the same depth-based price tracking strategy as leg 2, applied to leg 3 (short call).
     *          Uses counter escalation to move from best to second-level prices when fills are slow.
     *          Modifies orders when market prices change or when total quantities diverge.
     *          Includes safety fallback to best prices if second-level quotes become unavailable.
     *
     * @param p Reference to Portfolio containing leg symbols and portfolio ID.
     * @param s Reference to StrategyMarketSnapshot (unused but kept for interface consistency).
     * @param o Reference to OrderManager for sending order modifications.
     * @param params Reference to strategy parameters (unused but kept for interface consistency).
     * @param order_data Reference to order tracking data containing leg 3 state, quantities, and prices.
     * @param modified Reference to boolean flag; set to true if order modification was sent successfully.
     * @param exe_time Execution timestamp passed to order manager for order timing.
     *
     * @return void This function modifies order_data in-place and updates the modified flag.
     *
     * @details Logic: Identical to handleLeg2Modifications but operates on leg 3 data structures
     *          (leg3_counter, leg3_depth, leg3_order_id, leg3_pending_qty, leg3_filled_qty, etc.)
     *          and uses p.legs[2] symbol token for order construction.
     *
     * @note See handleLeg2Modifications documentation for detailed execution flow.
     */
    static void handleLeg3Modifications(Portfolio &p,
                                        const StrategyMarketSnapshot &s,
                                        OrderManager &o,
                                        const auto &params,
                                        auto &order_data, bool &modified, const unsigned long long &exe_time) noexcept
    {
        LOG_FILE("BoxBidding", "Enetered 3rd Modification");

        // ========== DEPTH ESCALATION COUNTER ==========
        p.updated_tick = false;
        order_data.leg3_counter++;

        if (order_data.leg3_counter >= 4)
        {
            order_data.leg3_depth = 1;
            order_data.leg3_counter = 0;
        }

        // ========== PRICE RETRIEVAL ==========
        uint32_t current_leg3_price;
        if (order_data.leg3_depth == 0)
        {
            // Use best bid/ask (top of book)
            current_leg3_price = getNonBiddingLegsPrice(order_data.leg3, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[0], order_data.snap.data.box_bidding.long_call.bids[0],
                                                        order_data.snap.data.box_bidding.long_put.asks[0], order_data.snap.data.box_bidding.long_put.bids[0],
                                                        order_data.snap.data.box_bidding.short_call.asks[0], order_data.snap.data.box_bidding.short_call.bids[0],
                                                        order_data.snap.data.box_bidding.short_put.asks[0], order_data.snap.data.box_bidding.short_put.bids[0]);
        }
        else
        {
            // Use second-level bid/ask (escalated level)
            current_leg3_price = getNonBiddingLegsPrice(order_data.leg3, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[1], order_data.snap.data.box_bidding.long_call.bids[1],
                                                        order_data.snap.data.box_bidding.long_put.asks[1], order_data.snap.data.box_bidding.long_put.bids[1],
                                                        order_data.snap.data.box_bidding.short_call.asks[1], order_data.snap.data.box_bidding.short_call.bids[1],
                                                        order_data.snap.data.box_bidding.short_put.asks[1], order_data.snap.data.box_bidding.short_put.bids[1]);
        }

        // ========== DEPTH FALLBACK SAFETY CHECK ==========
        if (current_leg3_price == 0 && order_data.leg3_depth == 1)
        {
            order_data.leg3_depth = 0;
            current_leg3_price = getNonBiddingLegsPrice(order_data.leg3, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[0], order_data.snap.data.box_bidding.long_call.bids[0],
                                                        order_data.snap.data.box_bidding.long_put.asks[0], order_data.snap.data.box_bidding.long_put.bids[0],
                                                        order_data.snap.data.box_bidding.short_call.asks[0], order_data.snap.data.box_bidding.short_call.bids[0],
                                                        order_data.snap.data.box_bidding.short_put.asks[0], order_data.snap.data.box_bidding.short_put.bids[0]);
        }

        // ========== MODIFICATION TRIGGER CHECK ==========
        LOG_FILE("BoxBidding", "current_leg3_price:" + std::to_string(current_leg3_price) + ",order_data.last_leg3_price:" + std::to_string(order_data.last_leg3_price) + ",order_data.leg3_pending_qty :" + std::to_string(order_data.leg3_pending_qty) + ",order_data.last_leg3_qty:" + std::to_string(order_data.last_leg3_qty) + " oms id for this is :" + std::to_string(order_data.leg3_order_id));

        if (current_leg3_price > 0 &&
            (current_leg3_price != order_data.last_leg3_price || order_data.leg3_pending_qty + order_data.leg3_filled_qty != order_data.last_leg3_qty))
        {
            // ========== ORDER MODIFICATION ==========
            Leg leg3;
            uint32_t new_order_qty = order_data.leg3_filled_qty + order_data.leg3_pending_qty;

            // Construct modified leg with updated price and cumulative quantity
            leg3 = {p.legs[2].symbol_token, current_leg3_price, new_order_qty, BuySell(order_data.leg3, order_data.is_flip), 0, order_data.leg3_order_id};

            // Send modification; update tracking on success
            if (order_data.leg3_ack && o.sendModifyPlacement(p.portfolio_id, order_data.leg3_order_id, leg3, exe_time, 3))
            {
                order_data.leg3_ack = false; // Clear ACK flag, wait for new acknowledgment
                modified = true;             // Signal that modification was sent
            }
        }
    }

    /**
     * @brief Monitors and modifies leg 4 (long put) orders based on current market prices and fill status.
     * @details Implements the same depth-based price tracking strategy as legs 2 and 3, applied to leg 4 (long put).
     *          Uses counter escalation to move from best to second-level prices when fills are slow.
     *          Modifies orders when market prices change or when total quantities diverge.
     *          Includes safety fallback to best prices if second-level quotes become unavailable.
     *
     * @param p Reference to Portfolio containing leg symbols and portfolio ID.
     * @param s Reference to StrategyMarketSnapshot (unused but kept for interface consistency).
     * @param o Reference to OrderManager for sending order modifications.
     * @param params Reference to strategy parameters (unused but kept for interface consistency).
     * @param order_data Reference to order tracking data containing leg 4 state, quantities, and prices.
     * @param modified Reference to boolean flag; set to true if order modification was sent successfully.
     * @param exe_time Execution timestamp passed to order manager for order timing.
     *
     * @return void This function modifies order_data in-place and updates the modified flag.
     *
     * @details Logic: Identical to handleLeg2Modifications and handleLeg3Modifications but operates on
     *          leg 4 data structures (leg4_counter, leg4_depth, leg4_order_id, leg4_pending_qty,
     *          leg4_filled_qty, etc.) and uses p.legs[3] symbol token for order construction.
     *
     * @note See handleLeg2Modifications documentation for detailed execution flow.
     * @note Includes DEBUG logging for current_leg4_price for additional troubleshooting visibility.
     */
    static void handleLeg4Modifications(Portfolio &p,
                                        const StrategyMarketSnapshot &s,
                                        OrderManager &o,
                                        const auto &params,
                                        auto &order_data, bool &modified, const unsigned long long &exe_time) noexcept
    {
        LOG_FILE("BoxBidding", "Enetered 4th Modification");

        // ========== DEPTH ESCALATION COUNTER ==========
        p.updated_tick = false;
        order_data.leg4_counter++;

        if (order_data.leg4_counter >= 4)
        {
            order_data.leg4_depth = 1;
            order_data.leg4_counter = 0;
        }

        // ========== PRICE RETRIEVAL ==========
        uint32_t current_leg4_price;
        if (order_data.leg4_depth == 0)
        {
            // Use best bid/ask (top of book)
            current_leg4_price = getNonBiddingLegsPrice(order_data.leg4, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[0], order_data.snap.data.box_bidding.long_call.bids[0],
                                                        order_data.snap.data.box_bidding.long_put.asks[0], order_data.snap.data.box_bidding.long_put.bids[0],
                                                        order_data.snap.data.box_bidding.short_call.asks[0], order_data.snap.data.box_bidding.short_call.bids[0],
                                                        order_data.snap.data.box_bidding.short_put.asks[0], order_data.snap.data.box_bidding.short_put.bids[0]);
        }
        else
        {
            // Use second-level bid/ask (escalated level)
            current_leg4_price = getNonBiddingLegsPrice(order_data.leg4, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[1], order_data.snap.data.box_bidding.long_call.bids[1],
                                                        order_data.snap.data.box_bidding.long_put.asks[1], order_data.snap.data.box_bidding.long_put.bids[1],
                                                        order_data.snap.data.box_bidding.short_call.asks[1], order_data.snap.data.box_bidding.short_call.bids[1],
                                                        order_data.snap.data.box_bidding.short_put.asks[1], order_data.snap.data.box_bidding.short_put.bids[1]);
        }

        // ========== DEPTH FALLBACK SAFETY CHECK ==========
        if (current_leg4_price == 0 && order_data.leg4_depth == 1)
        {
            order_data.leg4_depth = 0;
            current_leg4_price = getNonBiddingLegsPrice(order_data.leg4, order_data.is_flip,
                                                        order_data.snap.data.box_bidding.long_call.asks[0], order_data.snap.data.box_bidding.long_call.bids[0],
                                                        order_data.snap.data.box_bidding.long_put.asks[0], order_data.snap.data.box_bidding.long_put.bids[0],
                                                        order_data.snap.data.box_bidding.short_call.asks[0], order_data.snap.data.box_bidding.short_call.bids[0],
                                                        order_data.snap.data.box_bidding.short_put.asks[0], order_data.snap.data.box_bidding.short_put.bids[0]);
        }

        // ========== MODIFICATION TRIGGER CHECK ==========
        LOG_FILE("BoxBidding", "current_leg4_price:" + std::to_string(current_leg4_price) + ",order_data.last_leg4_price:" + std::to_string(order_data.last_leg4_price) + ",order_data.leg4_pending_qty :" + std::to_string(order_data.leg4_pending_qty) + ",order_data.last_leg4_qty:" + std::to_string(order_data.last_leg4_qty) + " oms id for this is :" + std::to_string(order_data.leg4_order_id));

        if (current_leg4_price > 0 &&
            (current_leg4_price != order_data.last_leg4_price || order_data.leg4_pending_qty + order_data.leg4_filled_qty != order_data.last_leg4_qty))
        {
            // ========== ORDER MODIFICATION ==========
            Leg leg4;
            uint32_t new_order_qty = order_data.leg4_filled_qty + order_data.leg4_pending_qty;

            // Construct modified leg with updated price and cumulative quantity
            leg4 = {p.legs[3].symbol_token, current_leg4_price, new_order_qty, p.legs[3].side, 0, order_data.leg4_order_id};

            // Send modification; update tracking on success
            if (order_data.leg4_ack && o.sendModifyPlacement(p.portfolio_id, order_data.leg4_order_id, leg4, exe_time, 4))
            {
                order_data.leg4_ack = false; // Clear ACK flag, wait for new acknowledgment
                modified = true;             // Signal that modification was sent
            }
        }
    }

    /**
     * @brief Evaluates trading opportunities and initiates leg 1 order placement when conditions are favorable.
     * @details Core entry point logic for the box bidding strategy. Checks for stop requests, validates
     *          spread profitability, calculates optimal order quantity considering market depth and remaining
     *          lots, and submits leg 1 order to initiate the box spread trade. On successful order submission,
     *          transitions to LEG1_PENDING state. Returns false if order submission fails to allow state
     *          re-evaluation in the same cycle; returns true otherwise to exit the loop.
     *
     * @param p Reference to Portfolio containing strategy state, limits, and leg symbols.
     * @param s Reference to StrategyMarketSnapshot containing market data including bid/ask quantities.
     * @param o Reference to OrderManager for submitting orders.
     * @param params Reference to strategy parameters including thresholds, lot limits, and price adjustments.
     * @param order_data Reference to order tracking data to maintain state between calls.
     * @param current_time Current system timestamp in microseconds since epoch.
     * @param itm_call_ask Ask price for in-the-money call.
     * @param itm_call_bid Bid price for in-the-money call.
     * @param itm_put_ask Ask price for in-the-money put.
     * @param itm_put_bid Bid price for in-the-money put.
     * @param otm_call_ask Ask price for out-of-the-money call.
     * @param otm_call_bid Bid price for out-of-the-money call.
     * @param otm_put_ask Ask price for out-of-the-money put.
     * @param otm_put_bid Bid price for out-of-the-money put.
     * @param strike_diff Difference between strike prices.
     * @param exe_time Execution timestamp passed to order manager.
     *
     * @return bool True if function exits the state loop (opportunity rejected, leg1 price invalid,
     *              order qty zero, or order submission succeeded). False only if order submission fails,
     *              allowing immediate re-evaluation in the same cycle.
     *
     * @details Execution Flow:
     *   1. Initialize spread tracker and handle DEBUG mode price overrides
     *   2. Check for stop request; if set, mark iteration complete and return true
     *   3. Reset order data from any previous cycle
     *   4. Check spread profitability if enabled via params.is_opportunity:
     *      - If no opportunity exists, mark iteration complete and return true
     *   5. Calculate available quantity considering market depth:
     *      - Flip mode: use ask quantities for buying positions, bid quantities for selling
     *      - Normal mode: use bid quantities for buying positions, ask quantities for selling
     *   6. Calculate leg 1 entry price based on strategy mode and entry leg configuration
     *   7. Validate leg 1 price is non-zero; return true if invalid
     *   8. Calculate order quantity as minimum of:
     *      - params.sol (spread order limit)
     *      - params.max_lots - p.traded_qty (remaining lot capacity)
     *      - available_qty (market depth at desired price level)
     *   9. Validate order quantity is non-zero; return true if invalid
     *   10. Construct leg 1 order with calculated price, quantity, and correct buy/sell side
     *   11. Submit order via OrderManager; on success:
     *       - Transition to LEG1_PENDING state
     *       - Record order timer start time for timeout management
     *       - Initialize order tracking variables (pending qty, filled qty, etc.)
     *       - Increment new_max counter for modification limit tracking
     *       - Log order submission to strategy log with spread information
     *       - Return true to exit loop
     *   12. If submission fails, return false to allow re-evaluation
     *
     * @note Stop request handling: Immediately moves to COMPLETED state and marks iteration over,
     *       preventing further order placement attempts.
     * @note Order quantity calculation ensures conservative order sizing to respect market depth,
     *       remaining portfolio lot limits, and strategy position limits.
     * @note The leg1_ack semaphore prevents order submission attempts while awaiting OMS acknowledgment.
     * @note DEBUG mode overrides prices and quantities for testing without market data.
     * @note Spread opportunity check is configurable via params.is_opportunity; if disabled,
     *       trades proceed without profitability validation.
     */
    static bool handleIdleState(Portfolio &p,
                                const StrategyMarketSnapshot &s,
                                OrderManager &o,
                                const auto &params,
                                auto &order_data,
                                uint64_t current_time,
                                uint32_t itm_call_ask, uint32_t itm_call_bid,
                                uint32_t itm_put_ask, uint32_t itm_put_bid,
                                uint32_t otm_call_ask, uint32_t otm_call_bid,
                                uint32_t otm_put_ask, uint32_t otm_put_bid,
                                uint32_t strike_diff, const unsigned long long &exe_time) noexcept
    {
        // ========== INITIALIZATION ==========
        StrategyDataLog log;
        uint32_t spread = INT_MAX;

        // DEBUG MODE: Override prices for testing without market data
#ifdef DEBUG
        itm_call_ask = TEST_PRICE_LEG1;
        itm_put_ask = TEST_PRICE_LEG4;
        otm_put_bid = TEST_PRICE_LEG2;
        otm_call_bid = TEST_PRICE_LEG3;
#endif

        // ========== STOP REQUEST CHECK ==========
        // If external stop has been requested, halt all trading and mark cycle complete
        if (p.stop_requested)
        {
            order_data.state = BoxBiddingStates::COMPLETED;
            p.is_iter_over = true;
            return true;
        }

        // Reset order data for clean state (clear any residual data from prior cycles)
        resetOrderData(order_data);
        p.updated_tick = false;

        LOG_FILE("BoxBidding", "IN IDLE");

        bool is_flip = order_data.is_flip;

        // ========== SPREAD OPPORTUNITY CHECK ==========
        // Optional profitability validation before committing to order placement
        if (params.is_opportunity) // LIKELY to be true in production
        {
            bool opportunity = checkSpreadOpportunity(p, itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid,
                                                      otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid,
                                                      strike_diff, is_flip, params, spread);
            if (!opportunity)
            {
                // Spread does not meet profitability threshold
                LOG_FILE("BoxBidding", "Opprotunity is not there");
                p.is_iter_over = true;
                order_data.state = BoxBiddingStates::COMPLETED;
                return true;
            }
        }

        // ========== AVAILABLE QUANTITY CALCULATION ==========
        // Calculate maximum quantity available at desired price levels, considering market depth
        uint32_t available_qty;
        uint32_t leg1_price;

        if (is_flip)
        {
            // Flip mode (sell box): buy where others are selling, sell where others are buying

            // Min of: qty others are offering to sell longs + qty others are offering to sell shorts
            //         qty others are offering to buy the longs we're selling + qty others are offering to buy shorts we're selling
            available_qty = std::min({s.data.box_bidding.short_put.asks_qty[0],   // Buying short put (asks)
                                      s.data.box_bidding.short_call.asks_qty[0],  // Buying short call (asks)
                                      s.data.box_bidding.long_put.bids_qty[0],    // Selling long put (bids)
                                      s.data.box_bidding.long_call.bids_qty[0]}); // Selling long call (bids)

            leg1_price = calculateLeg1Price(itm_call_ask, itm_call_bid,
                                            itm_put_ask, itm_put_bid,
                                            otm_call_ask, otm_call_bid,
                                            otm_put_ask, otm_put_bid,
                                            strike_diff, order_data, params);
        }
        else
        {
            // Normal mode (buy box): buy where others are selling, sell where others are buying

            // Min of: qty others are offering to buy shorts + qty others are offering to buy shorts
            //         qty others are offering to sell the longs we're buying + qty others are offering to sell puts
            available_qty = std::min({s.data.box_bidding.short_put.bids_qty[0],   // Selling short put (bids)
                                      s.data.box_bidding.short_call.bids_qty[0],  // Selling short call (bids)
                                      s.data.box_bidding.long_put.asks_qty[0],    // Buying long put (asks)
                                      s.data.box_bidding.long_call.asks_qty[0]}); // Buying long call (asks)

            leg1_price = calculateLeg1Price(itm_call_ask, itm_call_bid,
                                            itm_put_ask, itm_put_bid,
                                            otm_call_ask, otm_call_bid,
                                            otm_put_ask, otm_put_bid,
                                            strike_diff, order_data, params);
        }

        // ========== LEG 1 PRICE VALIDATION ==========
        // Ensure calculated entry price is valid and non-zero
        if (leg1_price == 0 && order_data.leg1_ack)
        {
            LOG_FILE("BoxBidding", "Leg1 Price is zero");
            p.is_iter_over = true;
            order_data.state = BoxBiddingStates::COMPLETED;
            return true;
        }

        // DEBUG MODE: Override quantity for testing
#ifdef DEBUG
        available_qty = TEST_QTY_LEG1;
#endif

        // ========== ORDER QUANTITY CALCULATION ==========
        // Conservative quantity to respect all constraints: strategy limit, portfolio limit, market depth
        uint32_t order_qty = std::min({static_cast<uint32_t>(params.sol), // Strategy order limit per cycle
                                       params.max_lots - p.traded_qty,    // Remaining portfolio lot capacity
                                       available_qty});                   // Available market depth at target price

        LOG_FILE("BoxBidding", "Leg1 availaible_qty: " + std::to_string(available_qty) +
                                   "params.sol: " + std::to_string(params.sol) +
                                   "Leg1 max_lot: " + std::to_string(params.max_lots));

        // ========== ORDER QUANTITY VALIDATION ==========
        // Ensure we have valid quantity to place
        if (order_qty == 0 && order_data.leg1_ack)
        {
            LOG_FILE("BoxBidding", "Leg1 OrderQty = 0");
            p.is_iter_over = true;
            order_data.state = BoxBiddingStates::COMPLETED;
            return true;
        }

        // ========== LEG 1 ORDER CONSTRUCTION ==========
        // Build order struct with calculated parameters
        Leg leg1{
            p.legs[0].symbol_token,             // Leg 1 symbol identifier
            leg1_price,                         // Entry price
            order_qty,                          // Quantity to order
            BuySell(order_data.leg1, is_flip)}; // Correct buy/sell side based on strategy mode

        // ========== ORDER SUBMISSION ==========
        // Attempt to submit leg 1 order; only proceed if ACK available and submission succeeds
        if (order_data.leg1_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg1_order_id),
                                                        OrderType::Bidding, leg1, exe_time, true))
        {
            // ========== STATE TRANSITION TO LEG1_PENDING ==========
            order_data.state = BoxBiddingStates::LEG1_PENDING;

            // Record order entry time for timeout/duration monitoring
            order_data.leg1_timer_start = current_time;

            // Initialize order tracking for this cycle
            order_data.current_cycle_qty = order_qty;
            order_data.leg1_price = leg1_price;
            order_data.leg1_pending_qty = order_qty;
            order_data.leg1_filled_qty = 0;

            // Clear ACK to prevent resubmission while awaiting OMS acknowledgment
            order_data.leg1_ack = false;

            LOG_FILE("BoxBidding", "send placement of single order" + std::to_string(order_data.leg1_order_id));

            // Increment modification limit counter for tracking max operations on this order
            order_data.new_max++;

            // ========== STRATEGY LOGGING ==========
            // Populate comprehensive order entry log for monitoring and audit trail
            log.msg_type = StrategyState::NewOrder;
            log.pf_id = p.portfolio_id;
            log.oms_order_id = order_data.leg1_order_id;
            log.price = leg1_price;
            log.qty = order_qty;
            log.side = BuySell(order_data.leg1, is_flip);
            log.token = p.legs[0].symbol_token;
            log.market_snapshot = order_data.snap;
            log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
            log.current_spread = spread;

            // Submit log to central strategy logging system
            UltraLog::strategy(log);

            return true;
        }

        // ========== ORDER SUBMISSION FAILURE ==========
        // If order submission failed, allow re-evaluation in same cycle
        LOG_FILE("BoxBidding", "sended placement fail of single order" + std::to_string(order_data.leg1_order_id) +
                                   "leg1_ack: " + std::to_string(order_data.leg1_ack));

        return true;
    }

    static bool handleLeg1PendingState(Portfolio &p,
                                       const StrategyMarketSnapshot &s,
                                       OrderManager &o,

                                       const auto &params,
                                       auto &order_data,
                                       uint64_t current_time,
                                       uint32_t itm_call_ask, uint32_t itm_call_bid,
                                       uint32_t itm_put_ask, uint32_t itm_put_bid,
                                       uint32_t otm_call_ask, uint32_t otm_call_bid,
                                       uint32_t otm_put_ask, uint32_t otm_put_bid,

                                       uint32_t strike_diff, const unsigned long long &exe_time) noexcept
    {
#ifdef DEBUG
        itm_call_ask = TEST_PRICE_LEG1;
        itm_put_ask = TEST_PRICE_LEG4;
        otm_put_bid = TEST_PRICE_LEG2;
        otm_call_bid = TEST_PRICE_LEG3;
#endif
        uint32_t spread = INT_MAX;
        StrategyDataLog log;
        // for testing
        LOG_FILE("BoxBidding", "IN LEG1 PENDING");

        // // Check if leg 1 partially filled (this would be set in partial fill callback)
        // if (order_data.leg1_partial_filled && order_data.leg1_filled_qty > 0)
        // {
        //     order_data.state = BoxBiddingStates::LEG1_PARTIAL_FILLED;
        //     return false;
        // }

        // // Check if leg 1 completely filled (this would be set in fill callback)
        // if (order_data.leg1_filled)
        // {
        //     order_data.state = BoxBiddingStates::LEG1_FILLED;
        //     return false;
        // }

        bool is_flip = order_data.is_flip;
        p.updated_tick = false;
        // Check timer for leg 1 modification/cancellation

        if (!p.stop_requested)
        {

            // Check if opportunity still exists
            bool opportunity_exists = params.is_opportunity & checkSpreadOpportunity(p, itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid, otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid, strike_diff, is_flip, params, spread);
            LOG_FILE("BoxBidding", "is_opportunity : " + std::to_string(params.is_opportunity) + " , " + std::to_string(checkSpreadOpportunity(p, itm_call_ask, itm_call_bid, itm_put_ask, itm_put_bid, otm_call_ask, otm_call_bid, otm_put_ask, otm_put_bid, strike_diff, is_flip, params, spread)));

            if (!opportunity_exists && params.is_opportunity)
            {
                LOG_FILE("BoxBidding", "IN LEG1 Cancel trying, order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id));

                // Cancel leg 1 order and go back to idle
                if (order_data.leg1_order_id != 0 && order_data.leg1_ack)
                {
                    LOG_FILE("BoxBidding", "IN LEG1 Cancel");

                    bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                    if (is_cancelled)
                    {
                        order_data.leg1_ack = false;
                        //order_data.state = BoxBiddingStates::IDLE;

                        log.msg_type = StrategyState::CancelOrder;

                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.price = order_data.leg1_price;
                        log.qty = order_data.leg1_pending_qty + order_data.leg1_filled_qty;
                        log.side = BuySell(order_data.leg1, is_flip);
                        log.token = p.legs[0].symbol_token;
                        log.market_snapshot = order_data.snap;
                        log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                        log.current_spread = spread;

                        UltraLog::strategy(log);
                    }
                }

                return true;
            }
            else if (order_data.mod_max > MOD_MAX || order_data.new_max > CANCEL_MAX)
            {
                LOG_FILE("BoxBidding", "IN LEG1 Cancel trying becASE MOD MAX REACH MOD_MAX_LIMIT, order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id));

                // Cancel leg 1 order and go back to idle
                if (order_data.leg1_order_id != 0 && order_data.leg1_ack)
                {

                    bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                    if (is_cancelled)
                    {

                        order_data.leg1_ack = false;

                        log.msg_type = StrategyState::CancelOrder;

                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.price = order_data.leg1_price;
                        log.qty = order_data.leg1_pending_qty + order_data.leg1_filled_qty;
                        log.side = BuySell(order_data.leg1, is_flip);
                        log.token = p.legs[0].symbol_token;
                        log.market_snapshot = order_data.snap;
                        log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                        log.current_spread = spread;
                        log.diff = p.params.box_bidding.leg1_spread_threshold;

                        UltraLog::strategy(log);

                        // order_data.state = BoxBiddingStates::COMPLETED;
                        return true;
                    }
                }

                return true;
            }
            else
            {
                uint32_t available_qty;
                uint32_t leg1_new_price;

                if (is_flip)
                {
                    available_qty = std::min({s.data.box_bidding.short_put.asks_qty[0], s.data.box_bidding.short_call.asks_qty[0], s.data.box_bidding.long_put.bids_qty[0], s.data.box_bidding.long_call.bids_qty[0]});
                    leg1_new_price = calculateLeg1Price(itm_call_ask, itm_call_bid,
                                                        itm_put_ask, itm_put_bid,
                                                        otm_call_ask, otm_call_bid,
                                                        otm_put_ask, otm_put_bid,
                                                        strike_diff, order_data, params);
                }
                else
                {
                    available_qty = std::min({s.data.box_bidding.short_put.bids_qty[0], s.data.box_bidding.short_call.bids_qty[0], s.data.box_bidding.long_put.asks_qty[0], s.data.box_bidding.long_call.asks_qty[0]});
                    // available_qty = 150;
                    leg1_new_price = calculateLeg1Price(itm_call_ask, itm_call_bid,
                                                        itm_put_ask, itm_put_bid,
                                                        otm_call_ask, otm_call_bid,
                                                        otm_put_ask, otm_put_bid,
                                                        strike_diff, order_data, params);
                }

#ifdef DEBUG
                available_qty = TEST_QTY_LEG1; // Commented for testing
#endif

                if (leg1_new_price == 0)
                {
                    std::cerr << "LEG 1 price is 0 in LEG1_PENDING_STATE" << std::endl;
                    LOG_FILE("BoxBidding", "IN LEG1 New price 0");

                    return true;
                }

                // here what if leg1 fill grreater than sol let say everytime cover happened imeediatly sol 1500, i place 750 and its covered i place other 750 and its covered than i place 750 thats also covered than this check
                uint32_t new_order_qty = std::min({params.sol - order_data.leg1_filled_qty, available_qty, params.max_lots - p.traded_qty});

                LOG_FILE("BoxBidding", "IN LEG1 Modifying : leg1_new_price:" + std::to_string(leg1_new_price) + ", order_data.leg1_price: " + std::to_string(order_data.leg1_price) + ", new_order_qty: " + std::to_string(new_order_qty) + " , order_data.leg1_pending_qty:" + std::to_string(order_data.leg1_pending_qty) + ", order_data.leg1_order_id:" + std::to_string(order_data.leg1_order_id));

                // Only modify if price or quantity changed
                if ((leg1_new_price != order_data.leg1_price || new_order_qty != order_data.leg1_pending_qty) && order_data.leg1_order_id != 0 && order_data.leg1_ack)
                {
                    LOG_FILE("BoxBidding", "IN LEG1 sure Modifying***********************************************");

                    Leg leg;
                    leg = {p.legs[0].symbol_token, leg1_new_price, order_data.leg1_filled_qty + new_order_qty, BuySell(order_data.leg1, is_flip)};

                    if (o.sendModifyPlacement(p.portfolio_id, order_data.leg1_order_id, leg, exe_time, 1))
                    {
                        order_data.leg1_ack = false;
                        order_data.leg1_price = leg1_new_price;
                        order_data.leg1_pending_qty = new_order_qty;
                        order_data.mod_max++;

                        log.msg_type = StrategyState::ModifyOrder;

                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.price = leg1_new_price;
                        log.qty = new_order_qty + order_data.leg1_filled_qty;
                        log.side = BuySell(order_data.leg1, is_flip);
                        log.token = p.legs[0].symbol_token;
                        log.market_snapshot = order_data.snap;
                        log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                        log.current_spread = spread;
                        log.diff = p.params.box_bidding.leg1_spread_threshold;
                        UltraLog::strategy(log);

                        LOG_FILE("BoxBidding", "Updated pending of single modify order" + std::to_string(order_data.leg1_pending_qty));
                    }
                    else{
                        return true;
                    }
                }
                else
                {
                    LOG_FILE("BoxBidding", "IN LEG1 sure Not Modifying");
                }
                order_data.leg1_timer_start = current_time; // Reset timer
            }
        }
        else if (p.stop_requested == true)
        {
            // Cancel leg 1 order and go back to idle
            LOG_FILE("BoxBidding", "order_data.leg1_order_id: " + std::to_string(order_data.leg1_order_id) + " order_data.leg1_ack: " + std::to_string(order_data.leg1_ack));
            if (order_data.leg1_order_id != 0 && order_data.leg1_ack)
            {
                LOG_FILE("BoxBidding", "IN LEG1 Cancel");

                // o.cancelOrder(order_data.leg1_order_id);
                bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                if (is_cancelled)
                {
                    order_data.leg1_ack = false;

                    log.msg_type = StrategyState::CancelOrder;

                    log.pf_id = p.portfolio_id;
                    log.oms_order_id = order_data.leg1_order_id;
                    log.price = order_data.leg1_price;
                    log.qty = order_data.leg1_pending_qty + order_data.leg1_filled_qty;
                    log.side = BuySell(order_data.leg1, is_flip);
                    log.token = p.legs[0].symbol_token;
                    log.market_snapshot = order_data.snap;
                    log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                    log.current_spread = spread;
                    log.diff = p.params.box_bidding.leg1_spread_threshold;
                    UltraLog::strategy(log);

                    return true;
                }
            }

            return true;
        }

        return true;
    }

    /**
     * @brief Manages leg 1 state when order receives partial fills while cover legs are being placed.
     * @details PART 1 OF 3: Initialization, fill validation, completion check, and leg 1 modification logic.
     *          This is the core entry and monitoring phase for partial fill handling. Validates that partial
     *          fills exist, checks if all four legs are completely covered (coverd) and filled for completion,
     *          monitors spread opportunity, and adaptively modifies leg 1 price/quantity as cover legs fill.
     *          On completion (all legs filled and covered), updates portfolio traded quantity and resets for
     *          next cycle. Can transition to LEG1_FILLED state to trigger leg 2-4 order placement.
     *
     * @param p Reference to Portfolio containing traded quantity and stop signals.
     * @param s Reference to StrategyMarketSnapshot containing market bid/ask data.
     * @param o Reference to OrderManager for sending modifications and cancellations.
     * @param params Reference to strategy parameters including timeouts and spread thresholds.
     * @param order_data Reference to order tracking data with fill quantities and coverage tracking.
     * @param current_time Current system timestamp in microseconds since epoch.
     * @param itm_call_ask Ask price for in-the-money call.
     * @param itm_call_bid Bid price for in-the-money call.
     * @param itm_put_ask Ask price for in-the-money put.
     * @param itm_put_bid Bid price for in-the-money put.
     * @param otm_call_ask Ask price for out-of-the-money call.
     * @param otm_call_bid Bid price for out-of-the-money call.
     * @param otm_put_ask Ask price for out-of-the-money put.
     * @param otm_put_bid Bid price for out-of-the-money put.
     * @param strike_diff Difference between strike prices.
     * @param exe_time Execution timestamp passed to order manager.
     *
     * @return bool True to exit state loop (normal monitoring), false to re-evaluate in same cycle
     *              (completion detected, stop requested, or state transition).
     *
     * @details PART 1 Execution Flow:
     *   1. Apply DEBUG mode price overrides if enabled
     *   2. Capture current partial fill quantity (leg1_filled_qty)
     *   3. Validate partial fill is non-zero; if zero, revert to LEG1_PENDING and return false
     *   4. Check completion condition: all four legs filled AND all covered >= partial_filled_qty
     *      - If complete: update portfolio traded_qty, reset order data, transition to IDLE, return false
     *   5. If NOT stop_requested:
     *      a. Re-validate spread opportunity (if params.is_opportunity enabled)
     *      b. If opportunity lost:
     *         - Cancel leg 1 order
     *         - Transition to LEG1_FILLED (allow cover legs to complete)
     *         - Log cancellation
     *         - Return false for re-evaluation
     *      c. Else (opportunity exists):
     *         - Recalculate available market quantity
     *         - Recalculate leg 1 entry price
     *         - Calculate new pending quantity based on sol capacity and market depth
     *         - If price or quantity changed AND order valid:
     *           * Send modification with cumulative quantity (filled + new pending)
     *           * Update leg1_price, leg1_pending_qty, mod_max counter
     *           * Log modification
     *         - Reset timer for timeout tracking
     *   6. If stop_requested:
     *      - Cancel leg 1 order
     *      - Transition to LEG1_FILLED
     *      - Log cancellation
     *      - Return false
     *
     * @note This Part 1 handles: validation, completion detection, opportunity checking, leg 1 modifications
     * @note Part 2 handles: cover leg 2 order placement and modification
     * @note Part 3 handles: cover legs 3 and 4 order placement and modification
     * @note "Strictly no loop" behavior: most paths return true; state transitions return false
     * @note Completion triggers immediate reset and IDLE transition for next cycle
     * @note All four legs must be both FILLED and COVERED for trade completion
     */
    static bool handleLeg1PartialFilledState(Portfolio &p,
                                             const StrategyMarketSnapshot &s,
                                             OrderManager &o,
                                             const auto &params,
                                             auto &order_data,
                                             uint64_t current_time,
                                             uint32_t itm_call_ask, uint32_t itm_call_bid,
                                             uint32_t itm_put_ask, uint32_t itm_put_bid,
                                             uint32_t otm_call_ask, uint32_t otm_call_bid,
                                             uint32_t otm_put_ask, uint32_t otm_put_bid,
                                             uint32_t strike_diff, const unsigned long long &exe_time) noexcept
    {
        // ========== INITIALIZATION ==========
        // DEBUG MODE: Override prices for testing
#ifdef DEBUG
        itm_call_ask = TEST_PRICE_LEG1;
        itm_put_ask = TEST_PRICE_LEG4;
        otm_put_bid = TEST_PRICE_LEG2;
        otm_call_bid = TEST_PRICE_LEG3;
#endif

        uint32_t spread = INT_MAX;
        StrategyDataLog log;

        LOG_FILE("BoxBidding", "IN LEG1 Partial FILL");

        // ========== PARTIAL FILL VALIDATION ==========
        // Capture the partial fill quantity reported by fill callbacks
        const uint32_t partial_filled_qty = order_data.leg1_filled_qty;

        // Safety check: if no fills detected, revert to pending state for continued monitoring
        if (partial_filled_qty == 0)
        {
            order_data.state = BoxBiddingStates::LEG1_PENDING;
            return false; // Re-evaluate in same cycle
        }

        // ========== COMPLETION CHECK ==========
        // Trade completes when: all 4 legs are filled AND all are covered by cover legs
        const uint32_t total_covered = getTotalCoveredQty(order_data);
        LOG_FILE("BIDDING 4", "Total covered: " + std::to_string(total_covered) +
                                  " partial_filled_qty now: " + std::to_string(partial_filled_qty));

        /*if (total_covered >= partial_filled_qty && order_data.leg2_filled &&
            order_data.leg3_filled && order_data.leg4_filled)
        {
            LOG_FILE("BoxBidding", "In a never true condition3");

            // All legs fully covered and filled - trade successfully completed
            const uint32_t successfully_traded = std::min(total_covered, partial_filled_qty);
            order_data.traded_qty += successfully_traded;
            p.traded_qty += order_data.traded_qty;
            p.is_data_updated = true;

            LOG_FILE("BIDDING 4", "Updated traded_qty by: " + std::to_string(successfully_traded) +
                                      " Total traded_qty now: " + std::to_string(order_data.traded_qty));

            // Reset for next cycle
            resetOrderData(order_data);
            order_data.state = BoxBiddingStates::IDLE;
            return false; // Re-evaluate immediately for next opportunity
        }*/

        // ========== LEG 1 ADAPTIVE MODIFICATION ==========
        bool is_flip = order_data.is_flip;
        p.updated_tick = false;

        // Calculate elapsed time for timeout management
        const uint64_t elapsed = current_time - order_data.leg1_timer_start;

        if (p.stop_requested == false)
        {
            // LOG_COUT("param opportunity: " + std::to_string(params.is_opportunity));

            // Re-validate that spread opportunity still exists
            bool opportunity_exists = params.is_opportunity && checkSpreadOpportunity(p, itm_call_ask, itm_call_bid,
                                                                                      itm_put_ask, itm_put_bid,
                                                                                      otm_call_ask, otm_call_bid,
                                                                                      otm_put_ask, otm_put_bid,
                                                                                      strike_diff, is_flip, params, spread);

            // ========== CASE 1: NO OPPORTUNITY EXISTS ==========
            if (!opportunity_exists)
            {
                // Stop filling leg 1; move to cover leg completion
                if (order_data.leg1_order_id != 0 && order_data.leg1_ack)
                {
                    LOG_FILE("BoxBidding", "No opportunity exists and there is actually an order at leg1");

                    // Cancel remaining leg 1 order
                    if (o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id))
                    {
                        order_data.leg1_ack = false;

                        // Log cancellation event
                        log.msg_type = StrategyState::CancelOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.price = order_data.leg1_price;
                        log.qty = order_data.leg1_pending_qty + order_data.leg1_filled_qty;
                        log.side = BuySell(order_data.leg1, is_flip);
                        log.token = p.legs[0].symbol_token;
                        log.market_snapshot = order_data.snap;
                        log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                        log.current_spread = spread;
                        log.diff = p.params.box_bidding.leg1_spread_threshold;
                        UltraLog::strategy(log);
                    }

                    return true; // Re-evaluate in next cycle / Will reach here only if everything goes right
                }
            }
            // ========== CASE 2: OPPORTUNITY EXISTS - ADAPTIVE MODIFICATION ==========
            else
            {
                // Recalculate market conditions for adaptive leg 1 modifications
                uint32_t available_qty;
                uint32_t leg1_new_price;

                if (is_flip)
                {
                    // Flip mode: quantities available for selling positions
                    available_qty = std::min({s.data.box_bidding.short_put.asks_qty[0],
                                              s.data.box_bidding.short_call.asks_qty[0],
                                              s.data.box_bidding.long_put.bids_qty[0],
                                              s.data.box_bidding.long_call.bids_qty[0]});

                    leg1_new_price = calculateLeg1Price(itm_call_ask, itm_call_bid,
                                                        itm_put_ask, itm_put_bid,
                                                        otm_call_ask, otm_call_bid,
                                                        otm_put_ask, otm_put_bid,
                                                        strike_diff, order_data, params);
                }
                else
                {
                    // Normal mode: quantities available for buying positions
                    available_qty = std::min({s.data.box_bidding.short_put.bids_qty[0],
                                              s.data.box_bidding.short_call.bids_qty[0],
                                              s.data.box_bidding.long_put.asks_qty[0],
                                              s.data.box_bidding.long_call.asks_qty[0]});

                    leg1_new_price = calculateLeg1Price(itm_call_ask, itm_call_bid,
                                                        itm_put_ask, itm_put_bid,
                                                        otm_call_ask, otm_call_bid,
                                                        otm_put_ask, otm_put_bid,
                                                        strike_diff, order_data, params);
                }

                // DEBUG MODE: Override quantity for testing
#ifdef DEBUG
                available_qty = TEST_QTY_LEG1;
#endif

                // Validate new price is non-zero
                if (leg1_new_price == 0)
                {
                    return true;
                }

                LOG_FILE("BoxBidding", "Current available qty: " + std::to_string(available_qty));

                // ========== NEW QUANTITY CALCULATION ==========
                // Calculate additional leg 1 quantity needed for complete fill
                // Constraint: remaining sol capacity, market depth, remaining portfolio lot limit
                const uint32_t total_covered_filled = getTotalCoveredFilledQty(order_data);
                const uint32_t uncovered_qty = order_data.leg1_filled_qty > total_covered_filled ? order_data.leg1_filled_qty - total_covered_filled : 0;

                uint32_t remaining_sol_capacity = params.sol - order_data.leg1_filled_qty;

                uint32_t new_order_qty = std::min({remaining_sol_capacity,
                                                   available_qty,
                                                   params.max_lots - p.traded_qty});

                // ========== MODIFICATION TRIGGER ==========
                // Modify if: price changed OR quantity changed AND new qty > 0
                if ((leg1_new_price != order_data.leg1_price ||
                     (new_order_qty != order_data.leg1_pending_qty && new_order_qty > 0)) &&
                    order_data.leg1_order_id != 0 && order_data.leg1_ack)
                {
                    // Construct modified order with cumulative quantity
                    Leg leg;
                    // LOG_COUT("Modify on Leg1 for qty: " + std::to_string(order_data.leg1_filled_qty + new_order_qty) +
                    //          "filled qty: " + std::to_string(order_data.leg1_filled_qty) +
                    //          "at price: " + std::to_string(leg1_new_price));

                    leg = {p.legs[0].symbol_token, leg1_new_price,
                           order_data.leg1_filled_qty + new_order_qty,
                           BuySell(order_data.leg1, is_flip)};

                    // Send modification
                    if (o.sendModifyPlacement(p.portfolio_id, order_data.leg1_order_id, leg, exe_time, 1))
                    {
                        order_data.leg1_ack = false;
                        order_data.leg1_price = leg1_new_price;
                        order_data.leg1_pending_qty = new_order_qty;
                        order_data.mod_max++;

                        // Log modification event
                        log.msg_type = StrategyState::ModifyOrder;
                        log.pf_id = p.portfolio_id;
                        log.oms_order_id = order_data.leg1_order_id;
                        log.price = leg1_new_price;
                        log.qty = new_order_qty + order_data.leg1_filled_qty;
                        log.side = BuySell(order_data.leg1, is_flip);
                        log.token = p.legs[0].symbol_token;
                        log.market_snapshot = order_data.snap;
                        log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                        log.current_spread = spread;
                        log.diff = p.params.box_bidding.leg1_spread_threshold;
                        UltraLog::strategy(log);
                        LOG_FILE("BoxBidding", "Successfully modified leg1 order");
                    }
                }

                // Reset timer for timeout tracking
                order_data.leg1_timer_start = current_time;
            }
        }
        // ========== CASE 3: STOP REQUESTED ==========
        else if (p.stop_requested == true)
        {
            // Cancel leg 1 order on stop signal
            if (order_data.leg1_order_id && order_data.leg1_ack)
            {
                LOG_FILE("BoxBidding", "IN LEG1 Cancel");

                bool is_cancelled = o.sendCancelPlacement(p.portfolio_id, order_data.leg1_order_id);
                if (is_cancelled)
                {
                    order_data.leg1_ack = false;
                   
                    // Log cancellation event
                    log.msg_type = StrategyState::CancelOrder;
                    log.pf_id = p.portfolio_id;
                    log.oms_order_id = order_data.leg1_order_id;
                    log.price = order_data.leg1_price;
                    log.qty = order_data.leg1_pending_qty + order_data.leg1_filled_qty;
                    log.side = BuySell(order_data.leg1, is_flip);
                    log.token = p.legs[0].symbol_token;
                    log.market_snapshot = order_data.snap;
                    log.given_spread = is_flip ? params.flip_price_difference : params.price_difference;
                    log.current_spread = spread;
                    log.diff = p.params.box_bidding.leg1_spread_threshold;
                    UltraLog::strategy(log);
                }
            }
        }

        // ========== cover LEG ORDER RESET LOGIC ==========
        // Clear completed orders to allow placement of new hedging orders for additional leg 1 fills

        // // Reset leg 2 if completely filled
        // if (order_data.leg2_order_id != 0 && order_data.leg2_filled)
        // {
        //     LOG_FILE("BoxBidding", "In a never true condition0");
        //     LOG_FILE("BoxBidding", "Reseting leg2 stats");
        //     order_data.leg2_order_id = 0;
        //     order_data.leg2_filled_qty = 0;
        //     order_data.leg2_filled = false;
        //     order_data.leg2_pending_qty = 0;
        //     order_data.leg2_counter = 0;
        //     order_data.leg2_depth = 0;
        // }

        // // Reset leg 3 if completely filled
        // if (order_data.leg3_order_id != 0 && order_data.leg3_filled)
        // {
        //     LOG_FILE("BoxBidding", "Reseting leg3 stats");
        //     LOG_FILE("BoxBidding", "In a never true condition1");
        //     order_data.leg3_order_id = 0;
        //     order_data.leg3_filled = false;
        //     order_data.leg3_pending_qty = 0;
        //     order_data.leg3_counter = 0;
        //     order_data.leg3_depth = 0;
        // }

        // // Reset leg 4 if completely filled
        // if (order_data.leg4_order_id != 0 && order_data.leg4_filled)
        // {
        //     LOG_FILE("BoxBidding", "In a never true condition2");
        //     LOG_FILE("BoxBidding", "Reseting leg4 stats");
        //     order_data.leg4_order_id = 0;
        //     order_data.leg4_filled = false;
        //     order_data.leg4_pending_qty = 0;
        //     order_data.leg4_counter = 0;
        //     order_data.leg4_depth = 0;
        // }

        // ========== LEG 2 (SHORT PUT) HANDLING ==========
        // Calculate quantity of leg 2 that remains uncovered by existing orders
        const uint32_t leg2_uncovered = getLeg2UncoveredQty(order_data);
        LOG_FILE("BoxBidding", "Leg 2 uncovered:" + std::to_string(leg2_uncovered) +
                                   " leg2 order id:" + std::to_string(order_data.leg2_order_id));

        p.updated_tick = false;

        // Case 1: No leg 2 order exists and we have uncovered quantity to cover
        if (order_data.leg2_order_id == 0 && order_data.leg2_ack == true && leg2_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order because im in handle partial filled state leg2");

            // Construct new leg 2 order with passive pricing
            Leg leg2;
            uint32_t leg2_price = getNonBiddingLegsPrice(order_data.leg2, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg2 = {p.legs[1].symbol_token, leg2_price, leg2_uncovered, BuySell(order_data.leg2, is_flip)};

            // Submit new order
            if (order_data.leg2_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg2_order_id),
                                                            OrderType::Bidding, leg2, exe_time, false))
            {
                // Order placed successfully; initialize tracking variables
                order_data.leg2_ack = false;
                order_data.legs2_timer_start = current_time;
                order_data.leg2_pending_qty = leg2_uncovered;
                order_data.leg2_covered_qty += leg2_uncovered; // Mark this quantity as now under cover
                order_data.leg2_counter = 0;
                order_data.leg2_depth = 0;
            }
        }
        // Case 2: Leg 2 order exists but not completely filled - consider modification
        else if (!order_data.leg2_filled && order_data.leg2_order_id != 0)
        {
            LOG_FILE("BoxBidding", "Modifying order leg2 order id is: " + std::to_string(order_data.leg2_order_id));

            // Track old pending quantity for rollback on modification failure
            const uint32_t old_leg2_pending_qty = order_data.leg2_pending_qty;

            // Calculate new pending quantity including newly uncovered amount
            const uint32_t new_leg2_pending_qty = order_data.leg2_pending_qty + leg2_uncovered;

            // Check if enough time has elapsed since last modification
            const uint64_t elapsed_2 = current_time - order_data.legs2_timer_start;

            LOG_FILE("BoxBidding", "Modifying order because im in handle partial fille: " +
                                       std::to_string(new_leg2_pending_qty));

            // Trigger modification if: quantity changed OR timeout elapsed
            if ((new_leg2_pending_qty + order_data.leg2_filled_qty) != order_data.last_leg2_qty ||
                elapsed_2 > params.legs2_timeout_us)
            {
                LOG_FILE("BoxBidding", "Modifying surely order because im in handle partial filled: here i new leg2 qty " +
                                           std::to_string(new_leg2_pending_qty));

                // Update pending quantity before modification attempt
                order_data.leg2_pending_qty = new_leg2_pending_qty;

                // Attempt modification via dedicated handler
                bool modified = false;
                handleLeg2Modifications(p, s, o, params, order_data, modified, exe_time);

                if (modified)
                {
                    // Modification succeeded; update coverage tracking and reset timer
                    order_data.leg2_covered_qty += leg2_uncovered;
                    order_data.leg2_pending_qty = new_leg2_pending_qty;
                    order_data.legs2_timer_start = current_time;
                }
                else
                {
                    // Modification failed; rollback to previous pending quantity
                    order_data.leg2_pending_qty = old_leg2_pending_qty;
                }
            }
        }

        // ========== LEG 3 (SHORT CALL) HANDLING ==========
        // Calculate quantity of leg 3 that remains uncovered by existing orders
        const uint32_t leg3_uncovered = getLeg3UncoveredQty(order_data);
        LOG_FILE("BoxBidding", "Leg 3 uncovered:" + std::to_string(leg3_uncovered) +
                                   " leg 3 order id :" + std::to_string(order_data.leg3_order_id));


        // Case 1: No leg 3 order exists and we have uncovered quantity to cover
        if (order_data.leg3_order_id == 0 && order_data.leg3_ack == true && leg3_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order because im in handle partial filled state leg3");

            // Construct new leg 3 order with passive pricing
            Leg leg3;
            uint32_t leg3_price = getNonBiddingLegsPrice(order_data.leg3, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg3 = {p.legs[2].symbol_token, leg3_price, leg3_uncovered, BuySell(order_data.leg3, is_flip)};
            order_data.last_leg3_price = leg3_price;

            // Submit new order
            if (order_data.leg3_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg3_order_id),
                                                            OrderType::Bidding, leg3, exe_time, false))
            {
                // Order placed successfully; initialize tracking variables
                order_data.leg3_ack = false;
                order_data.legs3_timer_start = current_time;
                order_data.leg3_pending_qty = leg3_uncovered;
                order_data.leg3_covered_qty += leg3_uncovered; // Mark this quantity as now under cover
                order_data.leg3_counter = 0;
                order_data.leg3_depth = 0;
                order_data.last_leg3_qty = leg3_uncovered;
            }
        }
        // Case 2: Leg 3 order exists but not completely filled - consider modification
        else if (!order_data.leg3_filled && order_data.leg3_order_id != 0)
        {
            LOG_FILE("BoxBidding", "old 3 pending qty:" + std::to_string(order_data.leg3_pending_qty));

            // Track old pending quantity for rollback on modification failure
            const uint32_t old_leg3_pending_qty = order_data.leg3_pending_qty;

            // Calculate new pending quantity including newly uncovered amount
            const uint32_t new_leg3_pending_qty = order_data.leg3_pending_qty + leg3_uncovered;

            // Check if enough time has elapsed since last modification
            const uint64_t elapsed_3 = current_time - order_data.legs3_timer_start;

            // Trigger modification if: quantity changed OR timeout elapsed
            if ((new_leg3_pending_qty + order_data.leg3_filled_qty != order_data.last_leg3_qty) ||
                elapsed_3 > params.legs3_timeout_us)
            {
                // Update pending quantity before modification attempt
                order_data.leg3_pending_qty = new_leg3_pending_qty;

                // Attempt modification via dedicated handler
                bool modified = false;
                handleLeg3Modifications(p, s, o, params, order_data, modified, exe_time);

                if (modified)
                {
                    // Modification succeeded; update coverage tracking and reset timer
                    order_data.leg3_covered_qty += leg3_uncovered;
                    order_data.leg3_pending_qty = new_leg3_pending_qty;
                    LOG_FILE("BoxBidding", "Seting pending leg3 qty to modify because of filled  " +
                                               std::to_string(order_data.leg3_pending_qty) + ", filled leg3 is this :" +
                                               std::to_string(order_data.leg3_filled_qty));

                    order_data.legs3_timer_start = current_time;
                }
                else
                {
                    // Modification failed; rollback to previous pending quantity
                    order_data.leg3_pending_qty = old_leg3_pending_qty;
                }
            }
        }

        // ========== LEG 4 (LONG PUT) HANDLING ==========
        // Calculate quantity of leg 4 that remains uncovered by existing orders
        const uint32_t leg4_uncovered = getLeg4UncoveredQty(order_data);
        LOG_FILE("BoxBidding", "Leg 4 uncovered:" + std::to_string(leg4_uncovered) +
                                   " leg 4 order id :" + std::to_string(order_data.leg4_order_id));

        p.updated_tick = false;

        // Case 1: No leg 4 order exists and we have uncovered quantity to cover
        if (order_data.leg4_order_id == 0 && order_data.leg4_ack == true && leg4_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order because im in handle partial filled state leg4");

            // Construct new leg 4 order with passive pricing
            Leg leg4;
            uint32_t leg4_price = getNonBiddingLegsPrice(order_data.leg4, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg4 = {p.legs[3].symbol_token, leg4_price, leg4_uncovered, BuySell(order_data.leg4, is_flip)};

            // Submit new order
            if (order_data.leg4_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg4_order_id),
                                                            OrderType::Bidding, leg4, exe_time, false))
            {
                // Order placed successfully; initialize tracking variables
                order_data.leg4_ack = false;
                order_data.legs4_timer_start = current_time;
                order_data.leg4_pending_qty = leg4_uncovered;
                order_data.leg4_covered_qty += leg4_uncovered; // Mark this quantity as now under cover
                order_data.leg4_counter = 0;
                order_data.leg4_depth = 0;
            }
        }
        // Case 2: Leg 4 order exists but not completely filled - consider modification
        else if (!order_data.leg4_filled && order_data.leg4_order_id != 0)
        {
            LOG_FILE("BoxBidding", "old 4 pending qty:" + std::to_string(order_data.leg4_pending_qty));

            // Track old pending quantity for rollback on modification failure
            uint32_t old_leg4_pending_qty = order_data.leg4_pending_qty;

            // Calculate new pending quantity including newly uncovered amount
            const uint32_t new_leg4_pending_qty = order_data.leg4_pending_qty + leg4_uncovered;

            // Check if enough time has elapsed since last modification
            const uint64_t elapsed_4 = current_time - order_data.legs4_timer_start;

            // Trigger modification if: quantity changed OR timeout elapsed
            if ((new_leg4_pending_qty + order_data.leg4_filled_qty != order_data.last_leg4_qty) ||
                elapsed_4 > params.legs4_timeout_us)
            {
                // Update pending quantity before modification attempt
                order_data.leg4_pending_qty = new_leg4_pending_qty;

                // Attempt modification via dedicated handler
                bool modified = false;
                handleLeg4Modifications(p, s, o, params, order_data, modified, exe_time);

                if (modified)
                {
                    // Modification succeeded; update coverage tracking and reset timer
                    order_data.leg4_covered_qty += leg4_uncovered;
                    order_data.leg4_pending_qty = new_leg4_pending_qty;
                    LOG_FILE("BoxBidding", "Seting pending leg4 qty to modify because of filled  " +
                                               std::to_string(order_data.leg4_pending_qty) + ", filled leg4 is this :" +
                                               std::to_string(order_data.leg4_filled_qty));

                    order_data.legs4_timer_start = current_time;
                }
                else
                {
                    // Modification failed; rollback to previous pending quantity
                    order_data.leg4_pending_qty = old_leg4_pending_qty;
                }
            }
        }

        // ========== FINAL STATE CLEANUP AND CHECK ==========
        // Reset the partial fill flag - the callback has been processed
        order_data.leg1_partial_filled = false;

        // Check if leg 1 is now completely filled; if so, transition to next state
        if (order_data.leg1_filled)
        {
            order_data.state = BoxBiddingStates::LEG1_FILLED;

            // Return false to trigger re-evaluation if tick was updated (new data available)
            // Return true otherwise (normal exit for this cycle)
            return (p.updated_tick == true) ? false : true;
        }

        return true;
    }
    /**
     * @brief Initiates cover leg (2, 3, 4) order placement after leg 1 is completely filled.
     * @details Transition state executed when leg 1 order is 100% filled. Primary responsibilities:
     *          (1) Check if trade already complete (all 4 legs filled and covered), (2) Reset any completed
     *          cover orders to allow new hedging attempts, (3) Calculate uncovered quantities for each cover leg,
     *          (4) Place new cover orders or modify existing ones for any uncovered amounts, (5) Transition to
     *          LEGS234_PENDING state to monitor cover leg fill progress. This state bridges entry (leg 1)
     *          completion and full cover coverage, ensuring all required hedging is actively pursued.
     *
     * @param p Reference to Portfolio containing portfolio ID, traded quantity, and completion flags.
     * @param s Reference to StrategyMarketSnapshot containing market bid/ask data.
     * @param o Reference to OrderManager for submitting and modifying cover orders.
     * @param params Reference to strategy parameters including leg timeouts.
     * @param order_data Reference to order tracking data with fill quantities and coverage state.
     * @param current_time Current system timestamp in microseconds since epoch.
     * @param itm_call_ask Ask price for in-the-money call.
     * @param itm_call_bid Bid price for in-the-money call.
     * @param itm_put_ask Ask price for in-the-money put.
     * @param itm_put_bid Bid price for in-the-money put.
     * @param otm_call_ask Ask price for out-of-the-money call.
     * @param otm_call_bid Bid price for out-of-the-money call.
     * @param otm_put_ask Ask price for out-of-the-money put.
     * @param otm_put_bid Bid price for out-of-the-money put.
     * @param strike_diff Difference between strike prices.
     * @param exe_time Execution timestamp passed to order manager.
     *
     * @return bool Always returns true (exits state loop). Returns false only if complete trade
     *              detected (all 4 legs filled and covered) to transition to COMPLETED state.
     *
     * @details Execution Flow:
     *   1. Apply DEBUG mode overrides if enabled
     *   2. Calculate completion metrics:
     *      - total_qty_in_this_cycle: leg 1 filled quantity
     *      - total_covered: minimum coverage across all cover legs
     *      - total_traded: minimum fills across all 4 legs
     *   3. Early completion check: if total_traded >= total_qty_in_this_cycle
     *      - All 4 legs are completely filled and covered
     *      - Update portfolio traded quantity
     *      - Mark iteration complete (is_iter_over = true)
     *      - Transition to COMPLETED state
     *      - Return false to exit immediately
     *   4. Reset completed cover orders (if order filled and order_id != 0):
     *      - Clear order_id to 0 (allows new cover orders for additional fills)
     *      - Reset filled flag, pending qty, counter, depth
     *      - Applied to legs 2, 3, 4 independently
     *   5. Calculate uncovered quantities for all three cover legs:
     *      - leg2_uncovered = leg1_filled_qty - leg2_covered_qty
     *      - leg3_uncovered = leg1_filled_qty - leg3_covered_qty
     *      - leg4_uncovered = leg1_filled_qty - leg4_covered_qty
     *   6. For each cover leg (2, 3, 4):
     *      Case A: No order exists AND uncovered qty > 0 AND ACK available
     *              → Place new cover order with passive pricing and uncovered qty
     *              → On success: initialize timer, pending qty, covered qty, counter, depth
     *      Case B: Order exists AND not completely filled
     *              → Check if quantity increased or timeout elapsed
     *              → If yes: call handleLegXModifications to modify
     *              → On success: update covered qty, reset timer
     *              → On failure: restore old pending qty
     *   7. State transition to LEGS234_PENDING (monitor cover leg fills)
     *
     * @note This state is briefly held; immediately transitions to LEGS234_PENDING for cover monitoring.
     * @note Early completion check prevents unnecessary cover leg processing if trade is done.
     * @note Order reset logic enables sequential hedging across multiple cycles if needed.
     * @note Uncovered quantity calculations drive new order placement decisions.
     * @note Timeout-based modifications escalate order pursuit if fills are slow.
     * @note ACK semaphores prevent order submissions while awaiting OMS acknowledgment.
     */
    static bool handleLeg1FilledState(Portfolio &p,
                                      const StrategyMarketSnapshot &s,
                                      OrderManager &o,
                                      const auto &params,
                                      auto &order_data,
                                      uint64_t current_time,
                                      uint32_t itm_call_ask, uint32_t itm_call_bid,
                                      uint32_t itm_put_ask, uint32_t itm_put_bid,
                                      uint32_t otm_call_ask, uint32_t otm_call_bid,
                                      uint32_t otm_put_ask, uint32_t otm_put_bid,
                                      uint32_t strike_diff, const unsigned long long &exe_time) noexcept
    {
        // ========== INITIALIZATION ==========
        // DEBUG MODE: Override prices for testing
#ifdef DEBUG
        itm_call_ask = TEST_PRICE_LEG1;
        itm_put_ask = TEST_PRICE_LEG4;
        otm_put_bid = TEST_PRICE_LEG2;
        otm_call_bid = TEST_PRICE_LEG3;
#endif

        LOG_FILE("BoxBidding", "IN LEG1 FILLED");

        // ========== COMPLETION METRICS CALCULATION ==========
        // Determine overall fill and coverage status
        const uint32_t total_qty_in_this_cycle = order_data.leg1_filled_qty; // Leg 1 entry fills
        const uint32_t total_covered = getTotalCoveredQty(order_data);       // Min cover coverage
        const uint32_t total_traded = getTotalTradedQty(order_data);         // Min of all 4 leg fills

        // ========== EARLY COMPLETION CHECK ==========
        // If all 4 legs are completely filled and covered, trade is done
        // if (total_traded >= total_qty_in_this_cycle)
        // {
        //     LOG_FILE("BoxBidding", "In a never true condition4");

        //     // Update portfolio statistics
        //     order_data.traded_qty = total_traded;
        //     p.traded_qty += total_traded;
        //     p.is_data_updated = true;
        //     p.is_iter_over = true;

        //     LOG_FILE("BoxBidding", "LEG1_FILLED: Updated traded_qty by:  Total traded_qty now: " +
        //                                std::to_string(order_data.traded_qty));

        //     // Mark strategy complete and exit
        //     order_data.state = BoxBiddingStates::COMPLETED;
        //     return false; // Exit state machine
        // }

        // ========== cover ORDER RESET LOGIC ==========
        // Clear completed orders to allow new hedging for additional fills

        // // Reset leg 2 if completely filled
        // if (order_data.leg2_order_id != 0 && order_data.leg2_filled)
        // {
        //     LOG_FILE("BoxBidding", "In a never true condition5");
        //     LOG_FILE("BIDDING 4", "Reseting things for leg2");

        //     order_data.leg2_order_id = 0;
        //     order_data.leg2_filled = false;
        //     order_data.leg2_pending_qty = 0;
        //     order_data.leg2_counter = 0;
        //     order_data.leg2_depth = 0;
        // }

        // // Reset leg 3 if completely filled
        // if (order_data.leg3_order_id != 0 && order_data.leg3_filled)
        // {
        //     LOG_FILE("BoxBidding", "In a never true condition5");
        //     LOG_FILE("BIDDING 4", "Reseting things for leg3");

        //     order_data.leg3_order_id = 0;
        //     order_data.leg3_filled = false;
        //     order_data.leg3_pending_qty = 0;
        //     order_data.leg3_counter = 0;
        //     order_data.leg3_depth = 0;
        // }

        // // Reset leg 4 if completely filled
        // if (order_data.leg4_order_id != 0 && order_data.leg4_filled)
        // {
        //     LOG_FILE("BoxBidding", "In a never true condition6");
        //     LOG_FILE("BIDDING 4", "Reseting things for leg4");

        //     order_data.leg4_order_id = 0;
        //     order_data.leg4_filled = false;
        //     order_data.leg4_pending_qty = 0;
        //     order_data.leg4_counter = 0;
        //     order_data.leg4_depth = 0;
        // }

        // ========== UNCOVERED QUANTITY CALCULATIONS ==========
        // Determine how much of leg 1 still needs hedging
        const uint32_t leg2_uncovered = getLeg2UncoveredQty(order_data);
        const uint32_t leg3_uncovered = getLeg3UncoveredQty(order_data);
        const uint32_t leg4_uncovered = getLeg4UncoveredQty(order_data);

        bool is_flip = order_data.is_flip;

        // ========== LEG 2 (SHORT PUT) HANDLING ==========
        p.updated_tick = false;

        // Case 1: No leg 2 order exists and we have uncovered quantity to cover
        if (order_data.leg2_order_id == 0 && order_data.leg2_ack == true && leg2_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order in LEG1_FILLED state for leg2!");

            // Construct new leg 2 order with passive pricing
            Leg leg2;
            uint32_t leg2_price = getNonBiddingLegsPrice(order_data.leg2, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg2 = {p.legs[1].symbol_token, leg2_price, leg2_uncovered, BuySell(order_data.leg2, is_flip)};

            // Submit new order
            if (order_data.leg2_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg2_order_id),
                                                            OrderType::Bidding, leg2, exe_time, false))
            {
                // Order placed successfully; initialize tracking
                order_data.leg2_ack = false;
                order_data.legs2_timer_start = current_time;
                order_data.leg2_pending_qty = leg2_uncovered;
                order_data.leg2_covered_qty += leg2_uncovered;
                order_data.leg2_counter = 0;
                order_data.leg2_depth = 0;
            }
        }
        // Case 2: Leg 2 order exists but not completely filled - consider modification
        else if (!order_data.leg2_filled && order_data.leg2_order_id != 0)
        {
            const uint32_t old_leg2_pending_qty = order_data.leg2_pending_qty;
            const uint32_t new_leg2_pending_qty = order_data.leg2_pending_qty + leg2_uncovered;
            const uint64_t elapsed_2 = current_time - order_data.legs2_timer_start;

            // Trigger modification if: quantity changed OR timeout elapsed
            if ((new_leg2_pending_qty + order_data.leg2_filled_qty) != order_data.last_leg2_qty ||
                elapsed_2 > params.legs2_timeout_us)
            {
                bool modified = false;
                order_data.leg2_pending_qty = new_leg2_pending_qty;

                // Attempt modification via dedicated handler
                handleLeg2Modifications(p, s, o, params, order_data, modified, exe_time);

                if (modified)
                {
                    // Modification succeeded; update coverage and reset timer
                    order_data.leg2_covered_qty += leg2_uncovered;
                    order_data.leg2_pending_qty = new_leg2_pending_qty;
                    order_data.legs2_timer_start = current_time;
                }
                else
                {
                    // Modification failed; rollback to previous pending qty
                    order_data.leg2_pending_qty = old_leg2_pending_qty;
                }
            }
        }

        // ========== LEG 3 (SHORT CALL) HANDLING ==========
        // Case 1: No leg 3 order exists and we have uncovered quantity to cover
        if (order_data.leg3_order_id == 0 && order_data.leg3_ack == true && leg3_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order in LEG1_FILLED state for leg3!");

            // Construct new leg 3 order with passive pricing
            Leg leg3;
            uint32_t leg3_price = getNonBiddingLegsPrice(order_data.leg3, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg3 = {p.legs[2].symbol_token, leg3_price, leg3_uncovered, BuySell(order_data.leg3, is_flip)};

            // Submit new order
            if (order_data.leg3_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg3_order_id),
                                                            OrderType::Bidding, leg3, exe_time, false))
            {
                // Order placed successfully; initialize tracking
                order_data.leg3_ack = false;
                order_data.legs3_timer_start = current_time;
                order_data.leg3_pending_qty = leg3_uncovered;
                order_data.leg3_covered_qty += leg3_uncovered;
                order_data.leg3_counter = 0;
                order_data.leg3_depth = 0;
            }
        }
        // Case 2: Leg 3 order exists but not completely filled - consider modification
        else if (!order_data.leg3_filled && order_data.leg3_order_id != 0)
        {
            const uint32_t old_leg3_pending_qty = order_data.leg3_pending_qty;
            const uint32_t new_leg3_pending_qty = order_data.leg3_pending_qty + leg3_uncovered;
            const uint64_t elapsed_3 = current_time - order_data.legs3_timer_start;

            // Trigger modification if: quantity changed OR timeout elapsed
            if (new_leg3_pending_qty + order_data.leg3_filled_qty != order_data.last_leg3_qty ||
                elapsed_3 > params.legs3_timeout_us)
            {
                bool modified = false;
                order_data.leg3_pending_qty = new_leg3_pending_qty;

                // Attempt modification via dedicated handler
                handleLeg3Modifications(p, s, o, params, order_data, modified, exe_time);

                if (modified)
                {
                    // Modification succeeded; update coverage and reset timer
                    order_data.leg3_covered_qty += leg3_uncovered;
                    order_data.leg3_pending_qty = new_leg3_pending_qty;
                    order_data.legs3_timer_start = current_time;
                }
                else{
                    order_data.leg3_pending_qty = old_leg3_pending_qty;
                }
            }
        }

        // ========== LEG 4 (LONG PUT) HANDLING ==========
        // Case 1: No leg 4 order exists and we have uncovered quantity to cover
        if (order_data.leg4_order_id == 0 && order_data.leg4_ack && leg4_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order in LEG1_FILLED state for leg4!");

            // Construct new leg 4 order with passive pricing
            Leg leg4;
            uint32_t leg4_price = getNonBiddingLegsPrice(order_data.leg4, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg4 = {p.legs[3].symbol_token, leg4_price, leg4_uncovered, BuySell(order_data.leg4, is_flip)};

            // Submit new order
            if (order_data.leg4_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg4_order_id),
                                                            OrderType::Bidding, leg4, exe_time, false))
            {
                // Order placed successfully; initialize tracking
                order_data.leg4_ack = false;
                order_data.legs4_timer_start = current_time;
                order_data.leg4_pending_qty = leg4_uncovered;
                order_data.leg4_covered_qty += leg4_uncovered;
                order_data.leg4_counter = 0;
                order_data.leg4_depth = 0;
            }
        }
        // Case 2: Leg 4 order exists but not completely filled - consider modification
        else if (!order_data.leg4_filled && order_data.leg4_order_id != 0)
        {
            const uint32_t old_leg4_pending_qty = order_data.leg4_pending_qty;
            const uint32_t new_leg4_pending_qty = order_data.leg4_pending_qty + leg4_uncovered;
            const uint64_t elapsed_4 = current_time - order_data.legs4_timer_start;

            // Trigger modification if: quantity changed OR timeout elapsed
            if (new_leg4_pending_qty + order_data.leg4_filled_qty != order_data.last_leg4_qty ||
                elapsed_4 > params.legs4_timeout_us)
            {
                bool modified = false;
                order_data.leg4_pending_qty = new_leg4_pending_qty;

                // Attempt modification via dedicated handler
                handleLeg4Modifications(p, s, o, params, order_data, modified, exe_time);

                if (modified)
                {
                    // Modification succeeded; update coverage and reset timer
                    order_data.leg4_covered_qty += leg4_uncovered;
                    order_data.leg4_pending_qty = new_leg4_pending_qty;
                    order_data.legs4_timer_start = current_time;
                }
                else{
                    order_data.leg4_pending_qty = old_leg4_pending_qty;
                }
            }
        }

        // ========== STATE TRANSITION ==========
        // Transition to cover leg monitoring state
        // Condition: at least one cover leg has orders pending OR all have zero uncovered qty
        if ((leg3_uncovered <= 0 && order_data.leg3_ack) ||
            (leg2_uncovered <= 0 && order_data.leg2_ack) ||
            (leg4_uncovered <= 0 && order_data.leg4_ack))
        {
            // At least one leg has pending ACK and uncovered qty; monitor fills
            order_data.state = BoxBiddingStates::LEGS234_PENDING;
        }
        else if (leg2_uncovered == 0 || leg3_uncovered == 0 || leg4_uncovered == 0)
        {
            // All uncovered quantities handled; transition to monitoring
            order_data.state = BoxBiddingStates::LEGS234_PENDING;
        }

        p.updated_tick = false;
        return true;
    }

    /**
     * @brief Monitors cover leg (2, 3, 4) order fills and manages timeout-based modifications.
     * @details Final active monitoring state before trade completion. Manages independent timers for each
     *          cover leg, triggering modifications when timeouts elapse to escalate order pursuit. Places new
     *          cover orders for any remaining uncovered quantities if prior orders expired or failed. Continuously
     *          checks completion condition: all four legs traded completely AND all cover legs either filled or
     *          exhausted (no pending ACK). On completion, updates portfolio statistics and transitions to
     *          COMPLETED state. This is the "busy waiting" state where the strategy actively tries to fill
     *          remaining positions through adaptive modifications.
     *
     * @param p Reference to Portfolio containing portfolio ID and completion flags.
     * @param s Reference to StrategyMarketSnapshot containing current market data.
     * @param o Reference to OrderManager for order modifications and new placements.
     * @param params Reference to strategy parameters with individual leg timeout values.
     * @param order_data Reference to order tracking data with fill and timer state.
     * @param current_time Current system timestamp in microseconds since epoch.
     * @param itm_call_ask Ask price for in-the-money call.
     * @param itm_call_bid Bid price for in-the-money call.
     * @param itm_put_ask Ask price for in-the-money put.
     * @param itm_put_bid Bid price for in-the-money put.
     * @param otm_call_ask Ask price for out-of-the-money call.
     * @param otm_call_bid Bid price for out-of-the-money call.
     * @param otm_put_ask Ask price for out-of-the-money put.
     * @param otm_put_bid Bid price for out-of-the-money put.
     * @param strike_diff Difference between strike prices.
     * @param exe_time Execution timestamp passed to order manager.
     *
     * @return bool True in normal monitoring flow (continue looping). False only on trade completion
     *              to exit state machine and transition to COMPLETED.
     *
     * @details Execution Flow:
     *   1. Apply DEBUG mode overrides if enabled
     *   2. Calculate completion metrics:
     *      - total_traded: minimum fills across all 4 legs
     *      - total_covered: minimum coverage across cover legs
     *      - leg2/3/4_uncovered: quantities still requiring covers
     *   3. For each cover leg (2, 3, 4) in sequence:
     *      a. Calculate elapsed time since last timer start
     *      b. If elapsed > timeout AND order exists AND not filled:
     *         - Call handleLegXModifications to escalate order pursuit
     *         - On success: update coverage tracking, reset timer
     *      c. Else if no order exists AND uncovered qty > 0 AND ACK available:
     *         - Place new cover order with passive pricing
     *         - On success: initialize timer, pending qty, coverage, depth, counter
     *   4. Completion check: all four conditions must be true:
     *      - total_traded >= leg1_filled_qty (all fills proportional)
     *      - leg2: either completely filled OR (no order AND ACK exhausted)
     *      - leg3: either completely filled OR (no order AND ACK exhausted)
     *      - leg4: either completely filled OR (no order AND ACK exhausted)
     *      If all true:
     *      - Update portfolio traded quantity
     *      - Mark data updated and iteration over
     *      - Transition to COMPLETED state
     *      - Return false to exit state machine
     *   5. Default: return true to continue monitoring
     *
     * @note Each cover leg has independent timer (legs2/3/4_timer_start) for granular control.
     * @note Timeout escalation: each elapsed timeout triggers modification attempt via dedicated handlers.
     * @note Passive pricing via getNonBiddingLegsPrice ensures best available execution.
     * @note ACK exhaustion condition: (order_id == 0 && !ack) means no more order submissions possible.
     * @note Coverage tracking incremented on successful modifications for progress tracking.
     * @note Completion requires proportional fills: all legs must reach same minimum quantity traded.
     * @note This state may remain active for extended periods if fills are slow but steady.
     */
    static bool handleLegs234PendingState(Portfolio &p,
                                          const StrategyMarketSnapshot &s,
                                          OrderManager &o,
                                          const auto &params,
                                          auto &order_data,
                                          uint64_t current_time,
                                          uint32_t itm_call_ask, uint32_t itm_call_bid,
                                          uint32_t itm_put_ask, uint32_t itm_put_bid,
                                          uint32_t otm_call_ask, uint32_t otm_call_bid,
                                          uint32_t otm_put_ask, uint32_t otm_put_bid,
                                          uint32_t strike_diff, const unsigned long long &exe_time) noexcept
    {
        // ========== INITIALIZATION ==========
        // DEBUG MODE: Override prices for testing
#ifdef DEBUG
        itm_call_ask = TEST_PRICE_LEG1;
        itm_put_ask = TEST_PRICE_LEG4;
        otm_put_bid = TEST_PRICE_LEG2;
        otm_call_bid = TEST_PRICE_LEG3;
#endif

        LOG_FILE("BoxBidding", "IN LEG 2,3&4 PENDING STATE");

        // ========== COMPLETION METRICS CALCULATION ==========
        // Determine overall fill and coverage status across all four legs
        const uint32_t total_traded = getTotalTradedQty(order_data);     // Min of all 4 leg fills
        const uint32_t total_covered = getTotalCoveredQty(order_data);   // Min cover coverage
        const uint32_t leg2_uncovered = getLeg2UncoveredQty(order_data); // leg1_filled - leg2_covered
        const uint32_t leg3_uncovered = getLeg3UncoveredQty(order_data); // leg1_filled - leg3_covered
        const uint32_t leg4_uncovered = getLeg4UncoveredQty(order_data); // leg1_filled - leg4_covered

        bool is_flip = order_data.is_flip;

        // ========== LEG 2 (SHORT PUT) TIMEOUT-BASED MODIFICATION ==========
        // Calculate elapsed time since leg 2 last action (placement or modification)
        const uint64_t elapsed_2 = current_time - order_data.legs2_timer_start;
        LOG_FILE("BoxBidding", "Important information 2nd" + std::to_string(elapsed_2) + " ," +
                                   std::to_string(params.legs2_timeout_us) + "," + std::to_string(order_data.leg2_filled) +
                                   "," + std::to_string(order_data.leg2_order_id));

        // Case 1: Leg 2 timeout elapsed - escalate order pursuit via modification
        if (elapsed_2 > params.legs2_timeout_us && !order_data.leg2_filled && order_data.leg2_order_id != 0)
        {
            bool modified = false;
            LOG_FILE("BoxBidding", "elapsed so going Modify fun 2nd leg" + std::to_string(elapsed_2) +
                                       " ," + std::to_string(params.legs2_timeout_us));

            // Attempt timeout-triggered modification
            handleLeg2Modifications(p, s, o, params, order_data, modified, exe_time);

            if (modified)
            {
                //LOG_COUT("Remove after testing - 1");
                //order_data.leg2_covered_qty += leg2_uncovered;
                order_data.legs2_timer_start = current_time; // Reset timer for next escalation
            }
            
        }
        // Case 2: No leg 2 order - place new one if uncovered qty exists
        else if (order_data.leg2_order_id == 0 && order_data.leg2_ack && leg2_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order because im in handle legs234 state, placing leg2");

            // Construct new leg 2 order with passive pricing
            Leg leg2;
            uint32_t leg2_price = getNonBiddingLegsPrice(order_data.leg2, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg2 = {p.legs[1].symbol_token, leg2_price, leg2_uncovered, BuySell(order_data.leg2, is_flip)};

            // Submit new order
            if (order_data.leg2_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg2_order_id),
                                                            OrderType::Bidding, leg2, exe_time, false))
            {
                // Order placed successfully; initialize tracking
                order_data.leg2_ack = false;
                order_data.legs2_timer_start = current_time;
                order_data.leg2_pending_qty = leg2_uncovered;
                order_data.leg2_covered_qty += leg2_uncovered;
                order_data.leg2_counter = 0;
                //order_data.leg2_level = 1;
                order_data.last_leg2_qty = leg2_uncovered;
                order_data.leg2_depth = 0;
            }
        }

        // ========== LEG 3 (SHORT CALL) TIMEOUT-BASED MODIFICATION ==========
        // Calculate elapsed time since leg 3 last action
        const uint64_t elapsed_3 = current_time - order_data.legs3_timer_start;
        LOG_FILE("BoxBidding", "Important information 3rd" + std::to_string(elapsed_3) + " ," +
                                   std::to_string(params.legs3_timeout_us) + "," + std::to_string(order_data.leg3_filled) +
                                   "," + std::to_string(order_data.leg3_order_id));

        // Case 1: Leg 3 timeout elapsed - escalate order pursuit via modification
        if (elapsed_3 > params.legs3_timeout_us && !order_data.leg3_filled && order_data.leg3_order_id != 0)
        {
            bool modified = false;
            LOG_FILE("BoxBidding", "elapsed so going Modify fun 3rd leg" + std::to_string(elapsed_3) +
                                       " ," + std::to_string(params.legs3_timeout_us));

            // Attempt timeout-triggered modification
            handleLeg3Modifications(p, s, o, params, order_data, modified, exe_time);

            if (modified)
            {
                //order_data.leg3_covered_qty += leg3_uncovered;
                order_data.legs3_timer_start = current_time; // Reset timer for next escalation
            }
        }
        // Case 2: No leg 3 order - place new one if uncovered qty exists
        else if (order_data.leg3_order_id == 0 && order_data.leg3_ack && leg3_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order because im in handle legs234 state, placing leg3");

            // Construct new leg 3 order with passive pricing
            Leg leg3;
            uint32_t leg3_price = getNonBiddingLegsPrice(order_data.leg3, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg3 = {p.legs[2].symbol_token, leg3_price, leg3_uncovered, BuySell(order_data.leg3, is_flip)};

            // Submit new order
            if (order_data.leg3_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg3_order_id),
                                                            OrderType::Bidding, leg3, exe_time, false))
            {
                // Order placed successfully; initialize tracking
                order_data.leg3_ack = false;
                order_data.legs3_timer_start = current_time;
                order_data.leg3_pending_qty = leg3_uncovered;
                order_data.leg3_covered_qty += leg3_uncovered;
                order_data.leg3_counter = 0;
                //order_data.leg3_level = 1;
                order_data.last_leg3_qty = leg3_uncovered;
                order_data.leg3_depth = 0;
            }
        }

        // ========== LEG 4 (LONG PUT) TIMEOUT-BASED MODIFICATION ==========
        // Calculate elapsed time since leg 4 last action
        const uint64_t elapsed_4 = current_time - order_data.legs4_timer_start;
        LOG_FILE("BoxBidding", "Important information 4th" + std::to_string(elapsed_4) + " ," +
                                   std::to_string(params.legs4_timeout_us) + "," + std::to_string(order_data.leg4_filled) +
                                   "," + std::to_string(order_data.leg4_order_id));

        // Case 1: Leg 4 timeout elapsed - escalate order pursuit via modification
        if (elapsed_4 > params.legs4_timeout_us && !order_data.leg4_filled && order_data.leg4_order_id != 0)
        {
            bool modified = false;
            LOG_FILE("BoxBidding", "elapsed so going Modify fun 4th leg" + std::to_string(elapsed_4) +
                                       " ," + std::to_string(params.legs4_timeout_us));

            // Attempt timeout-triggered modification
            handleLeg4Modifications(p, s, o, params, order_data, modified, exe_time);

            if (modified)
            {
                //order_data.leg4_covered_qty += leg4_uncovered;
                order_data.legs4_timer_start = current_time; // Reset timer for next escalation
            }
        }
        // Case 2: No leg 4 order - place new one if uncovered qty exists
        else if (order_data.leg4_order_id == 0 && order_data.leg4_ack && leg4_uncovered > 0)
        {
            LOG_FILE("BoxBidding", "New  order because im in handle legs234 state, placing leg4");

            // Construct new leg 4 order with passive pricing
            Leg leg4;
            uint32_t leg4_price = getNonBiddingLegsPrice(order_data.leg4, is_flip,
                                                         itm_call_ask, itm_call_bid,
                                                         itm_put_ask, itm_put_bid,
                                                         otm_call_ask, otm_call_bid,
                                                         otm_put_ask, otm_put_bid);

            leg4 = {p.legs[3].symbol_token, leg4_price, leg4_uncovered, BuySell(order_data.leg4, is_flip)};

            // Submit new order
            if (order_data.leg4_ack && o.sendSingleLegOrder(p.portfolio_id, (order_data.leg4_order_id),
                                                            OrderType::Bidding, leg4, exe_time, false))
            {
                // Order placed successfully; initialize tracking
                order_data.leg4_ack = false;
                order_data.legs4_timer_start = current_time;
                order_data.leg4_pending_qty = leg4_uncovered;
                order_data.leg4_covered_qty += leg4_uncovered;
                order_data.leg4_counter = 0;
                //order_data.leg4_level = 1;
                order_data.last_leg4_qty = leg4_uncovered;
                order_data.leg4_depth = 0;
            }
        }

        // ========== COMPLETION CHECK ==========
        // Trade completes when: all 4 legs are filled with same qty filled AND all covers either done or exhausted
        // Conditions:
        // 1. total_traded >= leg1_filled_qty (all fills complete and proportional)
        // 2. leg2: either (filled=true) OR (no order AND ACK exhausted)
        // 3. leg3: either (filled=true) OR (no order AND ACK exhausted)
        // 4. leg4: either (filled=true) OR (no order AND ACK exhausted)
        if (total_traded >= order_data.leg1_filled_qty &&
            (order_data.leg1_filled) &&
            (order_data.leg2_filled) &&
            (order_data.leg3_filled) &&
            (order_data.leg4_filled))
        {
            LOG_FILE("BoxBidding", "TRADE COMPLETED: Updating statistics and transitioning to COMPLETED");

            // Update portfolio with final traded quantity
            order_data.traded_qty = total_traded;
            p.traded_qty += order_data.traded_qty; // Propagate to frontend/reporting
            p.is_data_updated = true;
            p.is_iter_over = true;

            // Log completion event
            LOG_FILE("BoxBidding", "LEGS234_PENDING: Updated traded_qty by: " + std::to_string(total_traded) +
                                       " Total traded_qty now: " + std::to_string(order_data.traded_qty));

            // Transition to final state
            order_data.state = BoxBiddingStates::COMPLETED;
            return false; // Exit state machine
        }

        // Continue monitoring cover legs
        return true;
    }

    /**
     * @brief Final state handler - checks completion conditions and optionally resets for next cycle.
     * @details Executed after all four legs have been filled or all opportunities exhausted. Primary
     *          responsibilities: (1) Verify portfolio traded quantity against max_lots limit, (2) If limit
     *          reached, mark strategy complete and terminate, (3) Otherwise, reset all order state and return
     *          to IDLE for next box spread cycle. This state is briefly held and immediately transitions
     *          to either strategy termination or IDLE for another cycle. Enables multi-cycle execution
     *          where the portfolio can execute multiple complete box spread trades up to max_lots limit.
     *
     * @param p Reference to Portfolio containing traded quantity and completion flags.
     * @param s Reference to StrategyMarketSnapshot (unused but kept for interface consistency).
     * @param o Reference to OrderManager (unused but kept for interface consistency).
     * @param params Reference to strategy parameters containing max_lots limit.
     * @param order_data Reference to order tracking data to reset for next cycle.
     * @param current_time Current system timestamp (unused but kept for interface consistency).
     * @param exe_time Execution timestamp (unused but kept for interface consistency).
     *
     * @return bool False if resetting for next cycle (IDLE state, trigger re-evaluation).
     *              True if terminating strategy (max_lots reached).
     *
     * @details Execution Flow:
     *   1. Log state entry for audit trail
     *   2. Log completion statistics: portfolio traded qty and max_lots limit
     *   3. Check max_lots termination condition:
     *      - If p.traded_qty >= params.max_lots:
     *        * Mark strategy inactive (is_active = false)
     *        * Set termination flags (terminate = true, is_iter_over = true)
     *        * Mark data updated for frontend reporting
     *        * Return true to exit state machine permanently
     *   4. If max_lots NOT reached (more cycles possible):
     *      - Reset all order data via resetOrderData() for clean slate
     *      - Clear modification counter for new cycle
     *      - Transition back to IDLE state
     *      - Return false to trigger immediate re-evaluation for next cycle
     *
     * @note This state acts as a "cleanup and decision point" between cycles.
     * @note Fast transition: either terminates or immediately goes to IDLE.
     * @note Multi-cycle support: portfolio can execute multiple box spreads sequentially.
     * @note Max_lots enforces portfolio position limits across all cycles.
     * @note Modification counter (new_max) resets each cycle for independent tracking.
     *
     * @see resetOrderData() for comprehensive state clearing
     * @see resetCoverLegStates() for cover leg-specific cleanup
     */
    static bool handleCompletedState(Portfolio &p,
                                     const StrategyMarketSnapshot &s,
                                     OrderManager &o,
                                     const auto &params,
                                     auto &order_data,
                                     uint64_t current_time, const unsigned long long &exe_time) noexcept
    {
        // ========== STATE ENTRY LOGGING ==========
        LOG_FILE("BoxBidding", "IN COMPLETE STATE");

        // ========== COMPLETION STATISTICS ==========
        LOG_FILE("BIDDING 4", "Completed strategy cycle: total_traded_qty: " + std::to_string(p.traded_qty) +
                                  " max_lots: " + std::to_string(params.max_lots));

        // ================ ERROR CATCHING =====================
        if (!order_data.leg1_ack)
        {
            std::cout << "Leg1 ack is false, but still my program is trying to complete the loop" << std::endl;
        } 

        if (order_data.q1 != p.traded_qty)
        {
            std::cout << "[BOX_ERROR] leg1 ack: false, leg1 is falling behind" << std::endl;
        } // leg1 filled > total traded

        if (!order_data.leg2_ack)
        {
            std::cout << "Leg2 ack is false, but still my program is trying to complete the loop" << std::endl;
        } 

        if (order_data.q2 != p.traded_qty)
        {
            std::cout << "[BOX_ERROR] leg2 ack: false, leg2 is falling behind" << std::endl;
        } // leg2 filled > total traded

        if (!order_data.leg3_ack)
        {
            std::cout << "Leg3 ack is false, but still my program is trying to complete the loop" << std::endl;
        }

        if (order_data.q3 != p.traded_qty)
        {
            std::cout << "[BOX_ERROR] leg3 ack: false, leg3 is falling behind" << std::endl;
        } // leg3 filled > total traded

        if (!order_data.leg4_ack)
        {
            std::cout << "Leg4 ack is false, but still my program is trying to complete the loop" << std::endl;
        }

        if (order_data.q4 != p.traded_qty)
        {
            std::cout << "[BOX_ERROR] leg4 ack: false, leg4 is falling behind" << std::endl;
        } // leg4 filled > total traded

        // if(OrderManager::sent_qty[p.portfolio_id] != p.traded_qty){
        //     std::cout << "[BOX_ERROR] Traded qty is not equal to Sent Qty, and it should be equal whenever a cycle is complete sent_qty: " << OrderManager::sent_qty[p.portfolio_id] << " traded_qty: " << p.traded_qty << std::endl;
        //     if(OrderManager::open_orders.find(p.portfolio_id) != OrderManager::open_orders.end() && OrderManager::open_orders[p.portfolio_id].size() > 0){
        //         std::cout << "[BOX_ERROR_CONTINUATION] Open orders are also there for this pf id = " + std::to_string(p.portfolio_id) + ", which are : " ;
        //         for(auto open_order_itr: OrderManager::open_orders[p.portfolio_id]){
        //             std::cout << open_order_itr << ' ';
        //         }
        //         std::cout << std::endl;
        //     }

        // } // Check if the total sent qty is truly equal to total sent qty | If not then print everything




        // ========== MAX LOTS TERMINATION CHECK ==========
        // Determine if portfolio has reached position limit
        if (p.traded_qty >= params.max_lots)
        {
            LOG_FILE("BIDDING 4", "Max lots reached, terminating strategy. traded and maxlots:" +
                                      std::to_string(p.traded_qty) + "," + std::to_string(params.max_lots));

            // Mark strategy as complete and inactive
            p.is_active = false;
            p.terminate = true;
            p.is_iter_over = true;
            p.is_data_updated = true; // Notify frontend of completion

            return true; // Exit state machine permanently
        }

        // ========== RESET FOR NEXT CYCLE ==========
        // Portfolio has not reached limit; prepare for another box spread cycle
        resetOrderData(order_data);
        order_data.new_max = 0; // Reset modification counter for new cycle
        order_data.state = BoxBiddingStates::IDLE;

        return false; // Trigger immediate re-evaluation for next cycle
    }

    /**
     * @brief Comprehensively resets all order tracking state for a fresh cycle.
     * @details Clears all fill flags, order IDs, quantities, timers, and state variables to enable
     *          a new independent box spread trade cycle. This is called when transitioning from COMPLETED
     *          state back to IDLE or at strategy initialization. Resets both entry leg (leg 1) and cover
     *          leg (legs 2, 3, 4) state completely. Ensures no state leakage between cycles.
     *
     * @param order_data Reference to order tracking data structure to reset.
     *
     * @return void Modifies order_data in-place.
     *
     * @details Reset Operations:
     *   1. Fill completion flags (all set to false):
     *      - leg1_filled, leg1_partial_filled
     *      - leg2_filled, leg3_filled, leg4_filled
     *   2. Order identifiers (all set to 0):
     *      - leg1_order_id, leg2_order_id, leg3_order_id, leg4_order_id
     *   3. ACK semaphores (all set to true - ready for new submissions):
     *      - leg1_ack, leg2_ack, leg3_ack, leg4_ack
     *   4. Quantity tracking (all set to 0):
     *      - traded_qty, current_cycle_qty
     *      - leg1_pending_qty, leg1_filled_qty
     *      - leg2/3/4 coverage, pending, actual fill quantities
     *   5. Price tracking:
     *      - leg1_price = 0
     *   6. Counters:
     *      - new_max = 0 (order placement counter)
     *      - mod_max = 0 (order modification counter)
     *   7. Delegate cover leg-specific cleanup to resetCoverLegStates()
     *
     * @note This ensures complete isolation between sequential cycles.
     * @note All quantities reset to 0 to start fresh accounting.
     * @note ACK flags set to true to enable order submissions.
     * @note Order IDs set to 0 to indicate no active orders.
     * @note Counters reset for independent limit tracking per cycle.
     *
     * @see resetCoverLegStates() for cover leg-specific reset details
     */
    static void resetOrderData(auto &order_data) noexcept
    {
        // ========== FILL FLAGS ==========
        // Clear all completion flags
        order_data.leg1_filled = false;
        order_data.leg1_partial_filled = false;
        order_data.leg2_filled = false;
        order_data.leg3_filled = false;
        order_data.leg4_filled = false;

        // ========== ORDER IDENTIFIERS ==========
        // Reset all order IDs to 0 (no active orders)
        order_data.leg1_order_id = 0;
        order_data.leg2_order_id = 0;
        order_data.leg3_order_id = 0;
        order_data.leg4_order_id = 0;

        // ========== ACK SEMAPHORES ==========
        // Reset all ACK flags to true (ready for new submissions)
        order_data.leg1_ack = true;
        order_data.leg2_ack = true;
        order_data.leg3_ack = true;
        order_data.leg4_ack = true;

        // ========== QUANTITY TRACKING ==========
        // Clear all cycle-level quantities
        order_data.traded_qty = 0;        // Completed trades this cycle
        order_data.current_cycle_qty = 0; // Current cycle entry quantity
        order_data.leg1_pending_qty = 0;  // Awaiting fill on leg 1
        order_data.leg1_filled_qty = 0;   // Completed fills on leg 1

        // ========== PRICE TRACKING ==========
        // Clear leg 1 price
        order_data.leg1_price = 0;

        // ========== cover LEG QUANTITIES ==========
        // Clear separate coverage tracking for cover legs
        order_data.leg2_covered_qty = 0;
        order_data.leg3_covered_qty = 0;
        order_data.leg4_covered_qty = 0;
        order_data.leg2_pending_qty = 0;
        order_data.leg3_pending_qty = 0;
        order_data.leg4_pending_qty = 0;
        order_data.leg2_actual_fill_qty = 0;
        order_data.leg3_actual_fill_qty = 0;
        order_data.leg4_actual_fill_qty = 0;

        // ========== OPERATION COUNTERS ==========
        // Reset modification limit trackers for new cycle
        // order_data.new_max = 0; // Order placement/cancellation counter
        order_data.mod_max = 0; // Order modification counter

        // ========== DELEGATE cover LEG RESET ==========
        // Perform comprehensive cover leg-specific cleanup
        resetCoverLegStates(order_data);
    }

    /**
     * @brief Resets all cover leg (2, 3, 4) specific state variables.
     * @details Clears fill flags, order IDs, quantities, timers, prices, and operational state for
     *          each cover leg independently. Called by resetOrderData() to ensure complete cleanup.
     *          Enables each cover leg to start fresh in the next cycle with no state carryover.
     *
     * @param order_data Reference to order tracking data structure to reset cover leg state in.
     *
     * @return void Modifies order_data cover leg state in-place.
     *
     * @details Per-Leg Reset (applied to legs 2, 3, 4):
     *   - Filled flag: set to false
     *   - Order ID: set to 0 (no active order)
     *   - Filled quantity: set to 0
     *   - Operational counters: counter and depth reset to 0
     *   - Price tracking: last_leg_X_price set to 0
     *   - Quantity tracking: last_leg_X_qty set to 0
     *   - Coverage quantities: leg_X_covered_qty set to 0
     *   - Pending quantities: leg_X_pending_qty set to 0
     *
     * @note Three identical reset sequences (legs 2, 3, 4) ensure consistent cleanup.
     * @note Order IDs set to 0 indicate no active orders to monitor or modify.
     * @note Counters (counter, depth) control depth escalation logic in modifications.
     * @note Last price and qty track previous order state for modification decisions.
     * @note Coverage and pending quantities track order progress toward cover completion.
     *
     * @see handleLeg2/3/4Modifications() which use counter/depth for escalation
     */
    static void resetCoverLegStates(auto &order_data) noexcept
    {
        // ========== LEG 2 (SHORT PUT) RESET ==========
        // Clear all leg 2 state for fresh start
        order_data.leg2_filled = false;  // Not yet filled in new cycle
        order_data.leg2_order_id = 0;    // No active order
        order_data.leg2_filled_qty = 0;  // No fills yet
        order_data.leg2_counter = 0;     // Reset depth escalation counter
        order_data.leg2_depth = 0;       // Start at best bid/ask level
        order_data.last_leg2_price = 0;  // Clear previous order price
        order_data.last_leg2_qty = 0;    // Clear previous order quantity
        order_data.leg2_covered_qty = 0; // No coverage tracked yet
        order_data.leg2_pending_qty = 0; // No pending quantities

        // ========== LEG 3 (SHORT CALL) RESET ==========
        // Clear all leg 3 state for fresh start
        order_data.leg3_filled = false;  // Not yet filled in new cycle
        order_data.leg3_order_id = 0;    // No active order
        order_data.leg3_filled_qty = 0;  // No fills yet
        order_data.leg3_counter = 0;     // Reset depth escalation counter
        order_data.leg3_depth = 0;       // Start at best bid/ask level
        order_data.last_leg3_price = 0;  // Clear previous order price
        order_data.last_leg3_qty = 0;    // Clear previous order quantity
        order_data.leg3_covered_qty = 0; // No coverage tracked yet
        order_data.leg3_pending_qty = 0; // No pending quantities

        // ========== LEG 4 (LONG PUT) RESET ==========
        // Clear all leg 4 state for fresh start
        order_data.leg4_filled = false;  // Not yet filled in new cycle
        order_data.leg4_order_id = 0;    // No active order
        order_data.leg4_filled_qty = 0;  // No fills yet
        order_data.leg4_counter = 0;     // Reset depth escalation counter
        order_data.leg4_depth = 0;       // Start at best bid/ask level
        order_data.last_leg4_price = 0;  // Clear previous order price
        order_data.last_leg4_qty = 0;    // Clear previous order quantity
        order_data.leg4_covered_qty = 0; // No coverage tracked yet
        order_data.leg4_pending_qty = 0; // No pending quantities

        //sizeof(FrontendMessage);
    }

    // ============================================================================
    // PORTFOLIO CALLBACK EVENT HANDLERS
    // ============================================================================
    // These friend functions are invoked in TemplateHelper to process the Portfolio of order lifecycle events. All functions operate on
    // order state, independent of other strategy kinds in same thread as events occur.
    // ============================================================================

    /**
     * @brief Handles exchange acknowledgment for a newly submitted order.
     * @details Invoked when the exchange acknowledges receipt of an order submission.
     *          Marks the order as acknowledged (clears ACK semaphore), allowing subsequent
     *          modifications or cancellations. Stores the acknowledged price and quantity
     *          for reference. This is the first confirmation that the order reached the
     *          exchange and is now eligible for matching.
     *
     * @param strat Reference to Portfolio receiving the acknowledgment.
     * @param strategy_order_id Unique order identifier assigned by strategy.
     * @param price Acknowledged order price (may differ if adjusted by exchange).
     * @param qty Acknowledged order quantity (may differ if partially rejected).
     *
     * @return void Modifies Portfolio state in-place; sets ACK flag for relevant leg.
     *
     * @note Called after order submission; enables ACK semaphore for future operations.
     * @note Price and qty may differ from submitted values if exchange applies restrictions.
     * @note Must identify which leg (1-4) this order_id belongs to via order_data lookup.
     */
    friend void handleExchangeAck(Portfolio &strat, uint32_t strategy_order_id, uint32_t price, uint32_t qty) noexcept;

    /**
     * @brief Handles exchange acknowledgment for an order modification request.
     * @details Invoked when the exchange acknowledges receipt of a modify request.
     *          Marks the modification as acknowledged (clears modification ACK flag),
     *          allowing subsequent modifications. Stores new price/qty parameters.
     *          This indicates the exchange has accepted the modification and applied it
     *          to the existing order in the market.
     *
     * @param strat Reference to Portfolio receiving the modification acknowledgment.
     * @param strategy_order_id Unique order identifier being modified.
     * @param price New acknowledged price from exchange.
     * @param qty New acknowledged quantity from exchange.
     *
     * @return void Modifies Portfolio state in-place; clears modification ACK flag.
     *
     * @note Called after modification submission; enables next modification attempt.
     * @note May involve tracking separate ACK state from initial submission ACK.
     * @note Price and qty reflect actual modification applied by exchange.
     */
    friend void handleExchangeModifyAck(Portfolio &strat, uint32_t strategy_order_id, uint32_t price, uint32_t qty) noexcept;

    /**
     * @brief Handles complete fill of an order.
     * @details Invoked when an order receives a fill that completes remaining quantity.
     *          Updates order fill tracking (leg_X_filled = true, leg_X_filled_qty = fill_qty).
     *          Triggers leg state transition if order is now 100% filled. Records fill price
     *          and checks if the current strategy cycle is completely filled. This is the primary flow for
     *          successfully completed orders.
     *
     * @param strat Reference to Portfolio receiving the fill notification.
     * @param strategy_order_id Unique order identifier being filled.
     * @param fill_qty Quantity filled in this execution.
     * @param fill_price Price at which fill occurred.
     * @param leg_id Reference to leg identifier (1-4); helps route update to correct leg tracking.
     * @param order_manager Reference to OrderManager for potential follow-up actions.
     * @param exe_time Execution timestamp when fill occurred.
     * @param orderbook Reference to market data tracking for latency/performance analysis.
     *
     * @return void Modifies Portfolio order_data in-place; sets fill flags and quantities.
     *
     * @details Fill Processing:
     *   - Identify which leg this order_id belongs to
     *   - Update leg_X_filled_qty with fill amount
     *   - Set leg_X_filled = true if 100% filled
     *   - Log fill event with price and quantity
     *   - Trigger fill callbacks that may transition state machine
     *
     * @note Complete fill: no remaining pending quantity for this order.
     * @note For cover legs: increment coverage tracking to mark coverd portions.
     * @note May trigger state transitions (e.g., LEG1_PENDING → LEG1_FILLED).
     * @note Orderbook reference used for latency measurement and optimization.
     */
    friend void handleFill(Portfolio &strat, uint32_t strategy_order_id, uint32_t fill_qty, uint32_t fill_price,
                           int32_t &leg_id, OrderManager &order_manager, unsigned long long exe_time,
                           ska::flat_hash_map<uint32_t, StoredMarketDataLatency> &orderbook) noexcept;

    /**
     * @brief Handles partial fill of an order.
     * @details Invoked when an order receives a partial fill (less than remaining quantity).
     *          Updates pending and filled quantities separately. Sets partial fill flag
     *          (leg_X_partial_filled = true) to trigger state machine to LEG1_PARTIAL_FILLED
     *          for cover leg placement. Continues monitoring for additional fills rather than
     *          transitioning to fully filled state. This enables concurrent hedging while leg 1
     *          continues to accept more fills.
     *
     * @param strat Reference to Portfolio receiving the partial fill notification.
     * @param strategy_order_id Unique order identifier receiving partial fill.
     * @param fill_qty Quantity filled in this partial execution.
     * @param fill_price Price at which partial fill occurred.
     * @param required_qty Total quantity required by this order.
     * @param leg_id Reference to leg identifier (1-4); helps route update.
     * @param order_manager Reference to OrderManager for follow-up actions.
     * @param exe_time Execution timestamp when fill occurred.
     * @param orderbook Reference to market data tracking.
     *
     * @return void Modifies Portfolio order_data in-place; sets partial fill flags and increments quantities.
     *
     * @details Partial Fill Processing:
     *   - Identify which leg this order_id belongs to
     *   - Increment leg_X_filled_qty by fill amount
     *   - Calculate remaining pending: required_qty - total_filled
     *   - Set leg_X_partial_filled = true (triggers state transition)
     *   - Do NOT set leg_X_filled = true (order still active for more fills)
     *   - For leg 1: allows concurrent cover leg placement
     *   - For cover legs: increment coverage tracking
     *   - Log partial fill event
     *
     * @note Partial fill: remaining quantity still pending on order.
     * @note Crucial for leg 1: enables cover legs to begin filling while leg 1 continues.
     * @note Triggers callback-driven state transition to LEG1_PARTIAL_FILLED.
     * @note Repeated partial fills accumulate in leg_X_filled_qty.
     * @note Order remains active in market after partial fill.
     */
    // friend void handlePartialFill(Portfolio &strat, uint32_t strategy_order_id, uint32_t fill_qty, uint32_t fill_price,
    //                               uint32_t required_qty, int32_t &leg_id, OrderManager &order_manager,
    //                               unsigned long long exe_time, ska::flat_hash_map<uint32_t, StoredMarketDataLatency> &orderbook) noexcept;

    /**
     * @brief Handles order rejection from exchange.
     * @details Invoked when exchange rejects an order submission (e.g., invalid price, symbol, size).
     *          Clears order tracking and may cancel any dependent orders. Doesn't immediately
     *          trigger retry; returns strategy to IDLE for next opportunity evaluation. This is
     *          a hard failure requiring manual intervention or next cycle attempt.
     *
     * @param strat Reference to Portfolio receiving the rejection.
     * @param strategy_order_id Unique order identifier that was rejected.
     * @param required_qty Total quantity that was rejected.
     * @param fill_qty_sum Any partial fills before rejection occurred.
     *
     * @return void Modifies Portfolio order_data in-place; clears order tracking.
     *
     * @details Rejection Processing:
     *   - Identify which leg this order_id belongs to
     *   - Clear order_id to 0 (remove from tracking)
     *   - Set ACK flag to true (allow resubmission)
     *   - Log rejection reason if available
     *   - Handle any fills_qty_sum that occurred before rejection
     *   - May trigger cancellation of dependent orders
     *   - Consider state reset or transition to IDLE
     *
     * @note Hard failure: order never reached matching engine.
     * @note May require strategy recalibration or manual restart.
     * @note Clears semaphore to allow retry on next cycle.
     */
    // friend void handleReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty, uint32_t fill_qty_sum) noexcept;

    /**
     * @brief Handles successful order cancellation.
     * @details Invoked when exchange confirms cancellation of a previously active order.
     *          Clears order tracking, preserves any partial fills, and may trigger state
     *          transitions. For leg 1: may reset to IDLE if no fills occurred. For cover legs:
     *          allows new orders to be placed. This is the normal cleanup path when orders
     *          are intentionally terminated.
     *
     * @param strat Reference to Portfolio whose order was cancelled.
     * @param strategy_order_id Unique order identifier that was cancelled.
     * @param required_qty Total quantity that was cancelled.
     * @param fill_qty_sum Partial fills that occurred before cancellation.
     * @param order_manager Reference to OrderManager for cleanup actions.
     * @param exe_time Execution timestamp when cancellation occurred.
     *
     * @return void Modifies Portfolio order_data in-place; clears order and may transition state.
     *
     * @details Cancellation Processing:
     *   - Identify which leg this order_id belongs to
     *   - Clear order_id to 0
     *   - Preserve fill_qty_sum in leg_X_filled_qty
     *   - Set ACK flag to true (enable new submissions)
     *   - Log cancellation with remaining qty: (required_qty - fill_qty_sum)
     *   - For leg 1: trigger state transition if partial fills exist
     *   - For cover legs: enable resubmission or transition
     *   - Update coverage if partial fills present
     *
     * @note Expected outcome: order successfully removed from market.
     * @note Partial fills preserved for accounting and hedging.
     * @note Enables next phase of strategy execution.
     */
    // friend void handleCancel(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty, uint32_t fill_qty_sum,
    //                          OrderManager &order_manager, unsigned long long exe_time) noexcept;

    /**
     * @brief Handles order rejection due to RMS (Risk Management System) rules.
     * @details Invoked when exchange rejects order for violating risk limits (e.g., position limits,
     *          notional exposure, max order size). Similar to handleReject but may indicate temporary
     *          condition (e.g., position limit recoverable). Cancels order and may not retry to next cycle.
     *
     * @param strat Reference to Portfolio whose order was RMS-rejected.
     * @param strategy_order_id Unique order identifier rejected by RMS.
     * @param required_qty Total quantity attempted.
     * @param fill_qty_sum Any partial fills before RMS rejection.
     * @param order_manager Reference to OrderManager for cleanup.
     * @param exe_time Execution timestamp of RMS rejection.
     *
     * @return void Modifies Portfolio order_data in-place; clears order and preserves fills.
     *
     * @details RMS Rejection Processing:
     *       stops the strategy immediately for safety
     *
     * @note RMS violations often temporary (position recovers, limits reset).
     * @note Distinguished from hard rejections; may warrant retry logic.
     * @note Partial fills preserved for accounting.
     */
    // friend void handleRMSReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty, uint32_t fill_qty_sum,
    //                             OrderManager &order_manager, unsigned long long exe_time) noexcept;

    /**
     * @brief Handles rejection of an order modification request.
     * @details Invoked when exchange rejects a modify request (e.g., price out of range, qty increase
     *          violating limits). Original order remains active with previous parameters. Allows retry
     *          with adjusted parameters or acceptance of current order state.
     *
     * @param strat Reference to Portfolio whose modification was rejected.
     * @param strategy_order_id Unique order identifier whose modify was rejected.
     * @param required_qty Quantity that was requested in the modification.
     *
     * @return void Modifies Portfolio order_data in-place; may retry or revert to prior state.
     *
     * @details Modify Rejection Processing:
     *   - Identify which leg this order_id belongs to
     *   - Revert to previous price/qty if stored
     *   - Set modify ACK flag to true (enable retry)
     *   - Log modification rejection reason
     *   - Keep order active with original parameters
     *   - May trigger alternative: cancel and resubmit
     *   - Consider alternative modification parameters for retry
     *
     * @note Original order unaffected; remains in market with prior params.
     * @note Allows selective retry or acceptance of current state.
     * @note May indicate market condition changes; recalculate before retry.
     */
    // friend void handleModifyReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty) noexcept;

    /**
     * @brief Handles rejection of an order cancellation request.
     * @details Invoked when exchange rejects a cancel request (rare but possible if order already
     *          filled or in process). Order remains active in market. Strategy continues monitoring
     *          or may attempt alternative cancellation. Indicates exchange system state mismatch;
     *          may require manual review.
     *
     * @param strat Reference to Portfolio whose cancellation was rejected.
     * @param strategy_order_id Unique order identifier whose cancel was rejected.
     * @param required_qty Original quantity for the order.
     *
     * @return void Modifies Portfolio order_data in-place; sets retry flag or logs alert.
     *
     * @details Cancel Rejection Processing:
     *   - Identify which leg this order_id belongs to
     *   - Set cancel ACK flag to true (enable retry)
     *   - Log cancel rejection reason
     *   - Order remains active and pending
     *   - May retry cancellation or accept active status
     *   - Flag for manual review if repeated cancellations rejected
     *
     * @note Rare condition; may indicate order already filled or exchange sync issue.
     * @note Order remains active unless manually intervened.
     * @note Potential dead letter scenario; track for alerts.
     */
    // friend void handleCancelReject(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty) noexcept;

    /**
     * @brief Handles order submission failure (network or utrade error).
     * @details Invoked when order submission fails due to network issues, timeout, or system errors
     *          (not exchange rejection). Distinguishes from handleReject which indicates exchange
     *          rejection. Clears order and allows retry; may backoff before next attempt. Indicates
     *          infrastructure issues rather than order/parameter problems.
     *
     * @param strat Reference to Portfolio whose order submission failed.
     * @param strategy_order_id Unique order identifier (may not have reached exchange).
     * @param required_qty Quantity that failed to submit.
     * @param fill_qty_sum Any partial fills (unlikely; order likely never submitted).
     * @param order_manager Reference to OrderManager for cleanup/retry.
     * @param exe_time Execution timestamp of failure.
     *
     * @return void Modifies Portfolio order_data in-place; clears order for retry.
     *
     * @details Request Failure Processing:
     *   - Identify which leg this order_id belongs to
     *   - Clear order_id to 0 (not in exchange)
     *   - Set ACK flag to true (enable retry)
     *   - Log submission failure reason (network timeout, etc.)
     *   - Implement backoff logic before retry
     *   - May indicate connection issues requiring attention
     *
     * @note Utrade/network error, not exchange rejection.
     * @note Order likely never reached exchange.
     * @note Retry-friendly; temporary issue expected.
     */
    // friend void handleRequestFailed(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty,
    //                                 uint32_t fill_qty_sum, OrderManager &order_manager, unsigned long long exe_time) noexcept;

    /**
     * @brief Handles order modification failure (network or system error).
     * @details Invoked when a modify request fails due to network, timeout, or system error (not
     *          exchange rejection). Original order remains unchanged and active. Allows retry with
     *          same or adjusted parameters. Distinguishes from handleModifyReject which indicates
     *          exchange-level rejection.
     *
     * @param strat Reference to Portfolio whose modification failed.
     * @param strategy_order_id Unique order identifier for the failed modification.
     * @param required_qty Quantity that was attempted in modification.
     * @param fill_qty_sum Current fills on the order.
     * @param order_manager Reference to OrderManager for cleanup/retry.
     * @param exe_time Execution timestamp of failure.
     *
     * @return void Modifies Portfolio order_data in-place; enables retry.
     *
     * @details Modify Failure Processing:
     *   - Identify which leg this order_id belongs to
     *   - Set modify ACK flag to true (enable retry)
     *   - Revert to previous price/qty parameters
     *   - Log modification failure reason
     *   - Keep order active with current parameters
     *   - Implement backoff before retry
     *   - May indicate connection issues
     *
     * @note Utrade/network error, not exchange rejection.
     * @note Original order unaffected and remains active.
     * @note Modification attempt did not reach exchange.
     */
    // friend void handleModifyFailed(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty,
    //                                uint32_t fill_qty_sum, OrderManager &order_manager, unsigned long long exe_time) noexcept;

    /**
     * @brief Handles order cancellation failure (network or system error).
     * @details Invoked when a cancel request fails due to network, timeout, or system error.
     *          Order remains active and cancellation may be retried. Distinguishes from
     *          handleCancelReject which indicates exchange-level rejection. Allows controlled
     *          retry or eventual order expiration.
     *
     * @param strat Reference to Portfolio whose cancellation attempt failed.
     * @param strategy_order_id Unique order identifier for the failed cancellation.
     * @param required_qty Total quantity on the order.
     * @param fill_qty_sum Current fills on the order.
     * @param order_manager Reference to OrderManager for cleanup/retry.
     * @param exe_time Execution timestamp of failure.
     *
     * @return void Modifies Portfolio order_data in-place; enables retry.
     *
     * @details Cancel Failure Processing:
     *   - Identify which leg this order_id belongs to
     *   - Set cancel ACK flag to true (enable retry)
     *   - Log cancellation failure reason
     *   - Order remains active and monitored
     *   - Implement backoff before retry
     *   - May eventually time out or let order fill naturally
     *   - Consider alternative strategies (modify instead of cancel)
     *
     * @note Utrade/network error, not exchange rejection.
     * @note Order remains active; cancel request did not reach exchange.
     * @note May eventually abandon cancel and accept order fills.
     */
    // friend void handleCancelFailed(Portfolio &strat, uint32_t strategy_order_id, uint32_t required_qty,
    //                                uint32_t fill_qty_sum, OrderManager &order_manager, unsigned long long exe_time) noexcept;
};
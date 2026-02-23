// HFTStrategyEngine.cpp
#include "HFTStrategyEngine.h"


/* ════════════════════════════════════════════════════════════
 * C ABI EXPORTS FOR STRATEGY .so FILES
 * ════════════════════════════════════════════════════════════ */
// Static API instance
PlatformAPI HFTStrategyEngine::s_platform_api;

extern "C" {

void platform_subscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    engine->token_to_portfolios[token].insert(pf_id);
    
    LOG_FILE("PLATFORM_API", "Subscribe: pf=" + std::to_string(pf_id) + 
             " token=" + std::to_string(token));
}

void platform_unsubscribe_token(PlatformContext* ctx, uint32_t pf_id, uint32_t token)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    auto it = engine->token_to_portfolios.find(token);
    if (it != engine->token_to_portfolios.end()) {
        it->second.erase(pf_id);
        if (it->second.empty()) {
            engine->token_to_portfolios.erase(it);
        }
    }
    
    LOG_FILE("PLATFORM_API", "Unsubscribe: pf=" + std::to_string(pf_id) + 
             " token=" + std::to_string(token));
}

} // extern "C"

using Side = OrderSide;

// construtor
// 1. seting market data tick max wait time market_data_threshold from config
// 2. creating SHM for redis
// 3. Reset and setting up redis host and port
HFTStrategyEngine::HFTStrategyEngine(const Config &cfg)
    : config(cfg),
      socketManager(cfg),
      trade_logger(cfg.shm_log_name, true)
{

    market_data_threshold = config.market_interval_ms;
    last_market_event_time = Clock::now();
    redis_db_shm_q = create_or_attach_shm<DBQueue>(config.shm_db_name.c_str(), true);

    // for (int i = 0; i < MAX_PORTFOLIOS; i++)
    // {
    //     portfolio_initialized[i] = false;
    // }

    for (int i = 0; i < MAX_PORTFOLIOS; i++)
    {
        portfolio_slots[i].allocated = false;
        portfolio_slots[i].plugin = nullptr;
        portfolio_slots[i].strategy_handle = nullptr;
    }

    plugin_count = 0;

    redis_host = config.redis_host;
    redis_port = config.redis_port;

    setup_platform_api();
}

HFTStrategyEngine::~HFTStrategyEngine()
{
}


void HFTStrategyEngine::setup_platform_api()
{
    s_platform_api.place_new_order_single_leg = api_place_new_order_single_leg;
    s_platform_api.place_new_order_multi_leg = api_place_new_order_multi_leg;
    s_platform_api.place_modify_order = api_place_modify_order;
    s_platform_api.place_cancel_order = api_place_cancel_order;
    s_platform_api.get_position       = api_get_position;
    s_platform_api.get_open_orders    = api_get_open_orders;
    s_platform_api.log_msg            = api_log_msg;
    s_platform_api.send_status_update = api_send_status_update;

    
    LOG_FILE(module, "Platform API initialized");
}

// Doing Recovery, and Network Initialisation
bool HFTStrategyEngine::initialize()
{
    if (!redis_db_recovered)
    {
        // recovery_redis_db();
        redis_db_recovered = true;
        // updating portfolio which are active in db hence strategy breaked so we have to make it stale.
        // for (int i = 0; i < MAX_PORTFOLIOS; i++)
        // {
        //     if (portfolio_initialized[i])
        //     {
        //         LOG_FILE(module, "Doing Stop for pf id:" + std::to_string(i));
        //         auto &portfolio = portfolios[i];
        //         if (portfolio.is_active)
        //         {
        //             LOG_FILE(module, "Doing STRATEGY_STALE for pf id:" + std::to_string(i));
        //             portfolio.is_active = false;
        //             portfolio.stale_status = StaleStatus::STRATEGY_STALE;
        //             push_portfolio(portfolio); // pushing portoflio to shm for db
        //         }
        //     }
        // }
    
    }

    if (!socketManager.initialize_network())
        return false;

    order_manager = std::make_unique<OrderManager>(
        socketManager.getUTradeSocket(), config.max_order_limit);

    open_orders.reserve(1024);

    std::string nse_fo_contract_file = get_nse_fo_contract_name();

    helper::load_nse_fo_contract_file(nse_fo_contract_file);


    LOG_LIVE(module,"initi");
    try
    {
        /* code */
        load_strategy_plugin("./strategies/conrev_ioc.so");
    }
    catch(const std::exception& e)
    {
        std::cout << e.what() << 'SO not loaded\n';
    }
    

    return true;
}

// // Recovery of redis code
// void HFTStrategyEngine::recovery_redis_db()
// {
//     std::cout << "Portfolios\n";
//     recover_portfolios(redis_host, redis_port, portfolios, token_to_portfolios, portfolio_initialized, config.redis_db_index);
//     std::cout << "bidding Params\n";
//     recover_three_leg_params(redis_host, redis_port, portfolios, config.redis_db_index);
//     std::cout << " bidding Order Data\n";
//     recover_three_leg_orders(redis_host, redis_port, portfolios, config.redis_db_index);

//     std::cout << "ioc Params\n";
//     recover_conrev_params(redis_host, redis_port, portfolios, config.redis_db_index);
//     std::cout << " ioc Order Data\n";
//     recover_conrev_order_data(redis_host, redis_port, portfolios, config.redis_db_index);

//     std::cout << "box bid Params\n";
//     recover_box_bidding_params(redis_host, redis_port, portfolios, config.redis_db_index);
//     std::cout << " box bid Order Data\n";
//     recover_box_bidding_orders(redis_host, redis_port, portfolios, config.redis_db_index);

//     std::cout << " Leg data\n";
//     recover_strategy_leg_data(redis_host, redis_port, oms_to_leg, config.redis_db_index);

//     std::cout << "box ioc Params\n";
//     recover_boxioc_params(redis_host, redis_port, portfolios, config.redis_db_index);
//     std::cout << " box ioc Order Data\n";
//     recover_boxioc_order_data(redis_host, redis_port, portfolios, config.redis_db_index);

//     std::cout << " Leg data\n";
//     recover_strategy_leg_data(redis_host, redis_port, oms_to_leg, config.redis_db_index);

//     for (const auto &[token, portfolios] : token_to_portfolios)
//     {
//         std::cout << "Token " << token << " → Portfolios: { ";
//         for (const auto &pid : portfolios)
//         {
//             std::cout << pid << " ";
//         }
//         std::cout << "}\n";
//     }

//     uint32_t highest_oms_id = 0;

//     for (const auto &[oms_id, leg] : oms_to_leg)
//     {
//         if (oms_id > highest_oms_id)
//         {
//             highest_oms_id = oms_id;
//         }
//         if (leg.order_state == OrderState::NewOms || leg.order_state == OrderState::NewExchange || leg.order_state == OrderState::ModifyOms || leg.order_state == OrderState::ModifyExchange || leg.order_state == OrderState::PartialFill)
//         {
//             open_orders[leg.portfolio_id].push_back(oms_id);
//         }
//     }

//     StrategyOrderIDManager::instance().set(highest_oms_id + 1); // Set starting point
// }

// Main code which continously see epoll fd and run respected case UTRADE, MARKET, FS
void HFTStrategyEngine::run()
{

    std::array<epoll_event, 1024> events;

    while (true)
    {

        auto now_clock = Clock::now();

        if (__builtin_expect( (now_clock - last_market_event_time > std::chrono::seconds(30)), 0))[[unlikely]]
        {
            for(auto stream_data : stream_to_sequence)
            {
                if(now_clock-stream_data.second.last_time >  std::chrono::seconds(30))[[unlikely]]
                {
                    std::unordered_set<uint16_t> portfolio_ids = stream_data.second.portfolio_ids;
                    for(auto portfolio_id : portfolio_ids)
                    {
                        std::cout<<"Stopping porfolio id due to tick stale :" << portfolio_id<<std::endl;
                        // Portfolio &strat = portfolios[portfolio_id]; 
                        // strat.stop_requested = true;
                        // strat.stop_reason = UpdateReason::TickStale;
                    }
                }
            }                
            last_market_event_time = now_clock; 
        }

        int event_count = epoll_wait(
            socketManager.getEpollFD(),
            events.data(),
            events.size(),
            0);

        for (int i = 0; i < event_count; ++i)
        {
            FDTag tag = static_cast<FDTag>(events[i].data.u64);
            switch (tag)
            {

            case FDTag::FrontendSocket:
            {
                socketManager.setupFrontendClient(); // 1 time
                break;
            }

            case FDTag::FrontendClient:
            {
                handle_frontend_event(); // 3 as per USER like how many portfolios & how many add / modify / delete / run
                break;
            }

            case FDTag::UTrade:
            {
                // ============================================================
                // STEP 1: Receive all available data into ring buffer
                // ============================================================
                while (true)
                {
                    size_t space = utrade_rx_buffer.contiguous_write_space();

                    if (space == 0) [[unlikely]]
                    {
                        LOG_FILE(module, "CRITICAL: UTrade RX buffer full");
                        LOG_LIVE(module, "CRITICAL: UTrade RX buffer full - possible protocol error");
                        break;
                    }

                    ssize_t bytes = recv(
                        socketManager.getUTradeSocket(),
                        utrade_rx_buffer.write_ptr(),
                        space,
                        0 // Non-blocking socket, no MSG_DONTWAIT needed
                    );

                    if (bytes < 0) [[unlikely]]
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            // No more data available - NORMAL exit
                            break;
                        }
                        else
                        {
                            perror("recv utrade");
                            LOG_FILE(module, "UTrade recv error: " + std::string(strerror(errno)));
                            break;
                        }
                    }

                    if (bytes == 0) [[unlikely]]
                    {
                        // UTrade connection closed
                        LOG_COUT("Disconnected utrade");
                        LOG_FILE(module, "Disconnected utrade");
                        LOG_LIVE(module, "Utrade is disconnected");
                        LOG_FILE(module, "Doing ForceStop as utrade disconnect");

                        // Force stop all active portfolios [[TODO]]
                        // for (int i = 0; i < MAX_PORTFOLIOS; i++)
                        // {
                        //     if (portfolio_initialized[i])
                        //     {
                        //         auto &portfolio = portfolios[i];
                        //         if (portfolio.is_active && (!portfolio.is_iter_over))
                        //         {
                        //             LOG_FILE(module, "Doing ForceStop for pf id:" + std::to_string(i));
                        //             LOG_LIVE(module, "Doing ForceStop for pf id with utrade stale:" + std::to_string(i));
                        //             portfolio.is_active = false;
                        //             portfolio.stale_status = StaleStatus::UTRADE_STALE;

                        //             push_portfolio(portfolio);

                        //             FrontendMessage ack_msg{};
                        //             ack_msg.msg_type = FrontendMessageType::UtradeDisconnected;
                        //             ack_msg.portfolio_id = i;

                        //             if (socketManager.getFrontendClientSocket() != -1)
                        //             {
                        //                 send(socketManager.getFrontendClientSocket(), &ack_msg,
                        //                      sizeof(ack_msg), MSG_DONTWAIT);
                        //             }
                        //         }
                        //         else if (portfolio.is_active)
                        //         {
                        //             LOG_FILE(module, "Doing ForceStop for pf id:" + std::to_string(i));
                        //             portfolio.is_active = false;

                        //             FrontendMessage ack_msg{};
                        //             ack_msg.msg_type = FrontendMessageType::StopAck;
                        //             ack_msg.portfolio_id = portfolio.portfolio_id;
                        //             ack_msg.ack.req_status = RequestStatus::Stopped;
                        //             ack_msg.ack.status_reason = StatusReason::UtradeDisconnect;

                        //             LOG_FILE(module, "Sending to frontend utrade disconnected so stopped normally");
                        //             if (socketManager.getFrontendClientSocket() != -1)
                        //             {
                        //                 send(socketManager.getFrontendClientSocket(), &ack_msg,
                        //                      sizeof(ack_msg), MSG_DONTWAIT);
                        //             }

                        //             push_portfolio(portfolio);
                        //         }
                        //     }
                        // }

                        // Try to reconnect
                        bool is_connected = socketManager.reconnectUTrade();

                        if (is_connected)
                        {
                            LOG_FILE(module, "Utrade is Connected going as it is.");
                            LOG_LIVE(module, "Utrade is Connected going as it is.");
                            utrade_rx_buffer.reset(); // Reset buffer for clean reconnection
                        }
                        else
                        {
                            LOG_FILE(module, "After 100000 Retries still utrade down doing force stop for all strategy");
                            LOG_LIVE(module, "After 100000 Retries still utrade down doing force stop for all strategy");

                            FrontendMessage connection_msg{};
                            connection_msg.msg_type = FrontendMessageType::UtradeDisconnected;

                            LOG_FILE(module, "Sending to frontend Utrade is disconnected");
                            if (socketManager.getFrontendClientSocket() != -1)
                            {
                                send(socketManager.getFrontendClientSocket(), &connection_msg,
                                     sizeof(connection_msg), MSG_DONTWAIT);
                            }

                            exit(1);
                        }
                        break;
                    }

                    // Successfully received bytes - commit to buffer
                    utrade_rx_buffer.commit_write(bytes);

                    LOG_FILE(module, "Received " + std::to_string(bytes) +
                                         " bytes from UTrade, buffer has " +
                                         std::to_string(utrade_rx_buffer.available()) + " bytes");
                }

                // ============================================================
                // STEP 2: Process all complete messages (ZERO copies!)
                // ============================================================
                while (utrade_rx_buffer.available() >= sizeof(OrderMessage))
                {
                    OrderMessage msg;

                    // Try zero-copy peek first (common case - no wrap)
                    const OrderMessage *msg_ptr = utrade_rx_buffer.peek();

                    if (msg_ptr) [[likely]]
                    {
                        // FAST PATH: Direct pointer access, zero copy!
                        msg = *msg_ptr;
                    }
                    else [[unlikely]]
                    {
                        // SLOW PATH: Message spans wrap point
                        if (!utrade_rx_buffer.read_wrapped(msg))
                        {
                            // Incomplete message, wait for more data
                            LOG_FILE(module, "Incomplete OrderMessage in buffer, waiting for more data");
                            break;
                        }
                    }

                    // Message is complete and valid - consume it from buffer
                    utrade_rx_buffer.consume(sizeof(OrderMessage));

                    // Process the message
                    auto st = __rdtsc();
                    push_logs(true, uint8_t(msg.type));

                    // Prepare update message for frontend
                    FrontendMessage update_msg{};
                    update_msg.msg_type = FrontendMessageType::OmsUpdate;

                    switch (msg.type)
                    {

                    case OrderMessageType::OrderAck:
                    {
                        uint16_t portfolio_id = msg.ack.portfolio_id;
                        uint8_t num_legs = msg.ack.num_legs;

                        for (uint8_t leg_idx = 0; leg_idx < num_legs; ++leg_idx)
                        {
                            uint32_t oms_order_id = msg.ack.legs[leg_idx].oms_order_id;
                            uint32_t token = msg.ack.legs[leg_idx].symbol_id;
                            OrderSide side = msg.ack.legs[leg_idx].side;

                            oms_to_leg[oms_order_id] = StrategyLegData{
                                .token = token,
                                .side = side,
                                .portfolio_id = portfolio_id,
                                .fill_price_sum = 0,
                                .fill_qty_sum = 0,
                                .required_qty = msg.ack.legs[leg_idx].qty,
                                .oms_order_id = msg.ack.legs[leg_idx].oms_order_id,
                                .order_state = OrderState::NewOms};

                            std::ostringstream leg_log;
                            leg_log << "OrderACK - Leg " << static_cast<int>(leg_idx + 1)
                                    << " | OMS ID: " << oms_order_id
                                    // << " | Strategy ID: " << strategy_order_id
                                    << " | Portfolio ID: " << portfolio_id
                                    << " | Qty: " << msg.ack.legs[leg_idx].qty;

                            // LOG_COUT(leg_log.str());
                            LOG_FILE(module, leg_log.str());

                            push_oms_data(oms_to_leg[oms_order_id]);
                        }

                        break;
                    }

                    case OrderMessageType::NewOrderAck:
                    {

                        uint64_t exchange_order_id = msg.new_ack.exchange_order_id;
                        uint32_t oms_order_id = msg.new_ack.oms_order_id;
                        uint32_t price = msg.new_ack.price;
                        uint32_t qty = msg.new_ack.qty;

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.exchange_order_id = exchange_order_id;
                        leg.order_state = OrderState::NewExchange;
                        leg.ordered_price = price;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        open_orders[portfolio_id].emplace_back(oms_order_id);

                        // Portfolio &strat = portfolios[portfolio_id];

                        // handleExchangeAck(strat, leg.oms_order_id, price, qty);

                        // LOG_COUT("OrdeACK came: " + std::to_string(oms_order_id));
                        LOG_FILE(module, "OrdeACK came: " + std::to_string(oms_order_id));

                        // LOG_COUT("OrdeACK came exchange_order_id: " + std::to_string(exchange_order_id));
                        LOG_FILE(module, "OrdeACK came exchange_order_id: " + std::to_string(exchange_order_id));

                        push_oms_data(leg);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

                        PortfolioSlot& slot = portfolio_slots[portfolio_id];
                        if (slot.allocated && slot.fn_table.on_order_update)
                        {
                            OrderUpdate upd;
                            upd.oms_order_id = oms_order_id;
                            upd.exchange_order_id = leg.exchange_order_id;
                            upd.token = leg.token;
                            upd.side = (uint8_t)leg.side;
                            upd.state = leg.order_state;
                            upd.ordered_price = leg.ordered_price;
                            upd.ordered_qty = leg.required_qty;
                            upd.filled_qty = leg.fill_qty_sum;
                            upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                                                leg.fill_price_sum / leg.fill_qty_sum : 0;
                            
                            slot.fn_table.on_order_update(
                                slot.strategy_handle,
                                (PlatformContext*)this,
                                portfolio_id,
                                &upd);
                        }
                        // push_order_data(strat);

                        break;
                    }

                    case OrderMessageType::ModifyAck:
                    {

                        uint32_t oms_order_id = msg.modify.oms_order_id;

                        old_oms_to_leg[oms_order_id] = oms_to_leg[oms_order_id];
                        oms_to_leg[oms_order_id].required_qty = msg.modify.new_leg.qty;
                        oms_to_leg[oms_order_id].ordered_price = msg.modify.new_leg.price;
                        oms_to_leg[oms_order_id].order_state = OrderState::ModifyOms;

                        std::ostringstream leg_log;
                        leg_log << "Modify OrderACK - Leg "
                                << " | OMS ID: " << oms_order_id
                                // << " | Strategy ID: " << strategy_order_id
                                << " | Portfolio ID: " << oms_to_leg[oms_order_id].portfolio_id
                                << " | Qty: " << msg.modify.new_leg.qty;

                        // LOG_COUT(leg_log.str());
                        LOG_FILE(module, leg_log.str());
                        push_oms_data(oms_to_leg[oms_order_id]);
                        break;
                    }

                    case OrderMessageType::ModifySuccess:
                    {

                        uint32_t oms_order_id = msg.modify_ack.oms_order_id;
                        uint64_t exchange_modified_time = msg.modify_ack.exchange_modified_time;
                        uint32_t price = msg.modify_ack.price;
                        uint32_t qty = msg.modify_ack.qty;
                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::ModifyExchange;
                        leg.exchange_modified_time = exchange_modified_time;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];

                        // handleExchangeModifyAck(strat, leg.oms_order_id, price, qty);

                        std::ostringstream leg_log;
                        leg_log << "Modify OrderACK - Leg "
                                << " | OMS ID: " << oms_order_id
                                << " | exchange_modified_time : " << exchange_modified_time
                                << " | Portfolio ID: " << oms_to_leg[oms_order_id].portfolio_id;

                        // LOG_COUT(leg_log.str());
                        LOG_FILE(module, leg_log.str()); //

                        push_oms_data(leg);
                        // push_order_data(strat);


                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::Fill:
                    {
                        uint32_t oms_order_id = msg.update.oms_order_id;
                        uint32_t fill_qty = msg.update.fill_qty;
                        uint32_t fill_price = msg.update.fill_price;
                        uint64_t exchange_order_id = msg.update.exchange_order_id;
                        uint64_t timestamp = msg.update.timestamp;

                        // LOG_COUT("Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::Fill;

                        leg.fill_qty_sum += fill_qty;
                        leg.fill_price_sum += msg.update.fill_price;
                        leg.exchange_modified_time = timestamp;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        auto &orders = open_orders[portfolio_id];
                        orders.erase(std::remove(orders.begin(), orders.end(), oms_order_id), orders.end());

                        // Portfolio &strat = portfolios[portfolio_id];

                        LOG_FILE(module, "oms order id: " + std::to_string(leg.oms_order_id));
                        LOG_FILE(module, "exchange time: " + std::to_string(leg.exchange_modified_time));
                        LOG_FILE(module, "Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id) + ", required qty :" + std::to_string(leg.required_qty));
                        LOG_FILE(module, "Fill received: fill qty sum=" + std::to_string(leg.fill_qty_sum));

                        // int32_t leg_id = -1;
                        // handleFill(strat, leg.oms_order_id, fill_qty, msg.update.fill_price, leg_id, *order_manager, st, orderbook);

                        // trade_logger.log(module.c_str(), leg.token, static_cast<int>(leg.side), static_cast<int>(strat.kind), portfolio_id, oms_order_id, msg.update.exchange_order_id, leg_id, fill_qty, msg.update.fill_price, msg.update.timestamp);
                        update_msg.portfolio_id = portfolio_id;

                        update_msg.update.is_active = true;
                        update_msg.update.terminate = false;

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;
                        //     LOG_FILE(module, "yes its complete stoping3");
                        //     update_msg.update.terminate = true;
                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 1");
                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::RecoveryFill:
                    {
                        uint32_t oms_order_id = msg.update.oms_order_id;
                        uint32_t fill_qty = msg.update.fill_qty;
                        uint32_t fill_price = msg.update.fill_price;
                        uint64_t exchange_order_id = msg.update.exchange_order_id;
                        uint64_t timestamp = msg.update.timestamp;

                        // LOG_COUT("Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));
                        LOG_FILE(module, "Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::Fill;

                        leg.fill_qty_sum += fill_qty;
                        leg.fill_price_sum += msg.update.fill_price;
                        leg.exchange_modified_time = timestamp;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        auto &orders = open_orders[portfolio_id];
                        orders.erase(std::remove(orders.begin(), orders.end(), oms_order_id), orders.end());

                        // Portfolio &strat = portfolios[portfolio_id];

                        LOG_FILE(module, "oms order id: " + std::to_string(leg.oms_order_id));
                        LOG_FILE(module, "Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id) + ", required qty :" + std::to_string(leg.required_qty));
                        LOG_FILE(module, "Fill received: fill qty sum=" + std::to_string(leg.fill_qty_sum));

                        int32_t leg_id = -1;

                        // trade_logger.log(module.c_str(), leg.token, static_cast<int>(leg.side), static_cast<int>(strat.kind), portfolio_id, oms_order_id, msg.update.exchange_order_id, leg_id, fill_qty, msg.update.fill_price, msg.update.timestamp);
                        update_msg.portfolio_id = portfolio_id;

                        update_msg.update.is_active = true;
                        update_msg.update.terminate = false;

                        push_oms_data(leg);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::PartialFill:
                    {
                        uint32_t oms_order_id = msg.update.oms_order_id;
                        uint32_t fill_qty = msg.update.fill_qty;
                        uint32_t fill_price = msg.update.fill_price;
                        uint64_t exchange_order_id = msg.update.exchange_order_id;
                        uint64_t timestamp = msg.update.timestamp;

                        // LOG_COUT("Partial Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));
                        LOG_FILE(module, "Partial Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::PartialFill;
                        leg.fill_qty_sum += fill_qty;
                        leg.fill_price_sum += msg.update.fill_price;
                        leg.exchange_modified_time = timestamp;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];

                        // int32_t leg_id = -1;
                        // handlePartialFill(strat, leg.oms_order_id, fill_qty, msg.update.fill_price, leg.required_qty, leg_id, *order_manager, st, orderbook);

                        // trade_logger.log(module.c_str(), leg.token, static_cast<int>(leg.side), static_cast<int>(strat.kind), portfolio_id, oms_order_id, msg.update.exchange_order_id, leg_id, fill_qty, msg.update.fill_price, msg.update.timestamp);

                        update_msg.portfolio_id = portfolio_id;

                        update_msg.update.is_active = true;
                        update_msg.update.terminate = false;

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     LOG_FILE(module, "yes its complete stoping4");
                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 3");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     update_msg.update.terminate = true;
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::RecoveryPartialFill:
                    {
                        uint32_t oms_order_id = msg.update.oms_order_id;
                        uint32_t fill_qty = msg.update.fill_qty;
                        uint32_t fill_price = msg.update.fill_price;
                        uint64_t exchange_order_id = msg.update.exchange_order_id;
                        uint64_t timestamp = msg.update.timestamp;

                        // LOG_COUT("Partial Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));
                        LOG_FILE(module, "Partial Fill received: Qty=" + std::to_string(fill_qty) + ", OMS ID= " + std::to_string(oms_order_id));

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::PartialFill;
                        leg.fill_qty_sum += fill_qty;
                        leg.fill_price_sum += msg.update.fill_price;
                        leg.exchange_modified_time = timestamp;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];

                        int32_t leg_id = -1;
                        // trade_logger.log(module.c_str(), leg.token, static_cast<int>(leg.side), static_cast<int>(strat.kind), portfolio_id, oms_order_id, msg.update.exchange_order_id, leg_id, fill_qty, msg.update.fill_price, msg.update.timestamp);

                        update_msg.portfolio_id = portfolio_id;

                        update_msg.update.is_active = true;
                        update_msg.update.terminate = false;

                        push_oms_data(leg);

                        break;
                    }

                    case OrderMessageType::NewReject:
                    {
                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        const uint32_t error_code = msg.reject.error_code;
                        const uint64_t exchange_order_id = msg.reject.exchange_order_id;
                        // LOG_COUT("reject came for this om id:" << oms_order_id);
                        LOG_FILE(module, "reject came for this om id:" + std::to_string(oms_order_id) + ", Reason is is" + helper::error_to_msg_str(error_code) + ", eexchange_order_id:" + std::to_string(exchange_order_id));

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::ExchnageRejected;

                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];

                        // // LOG_COUT("reject came for this pf id:" << portfolio_id);

                        // handleReject(strat, leg.oms_order_id, leg.required_qty, leg.fill_qty_sum);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;

                        //     update_msg.update.terminate = true;

                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 5");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     LOG_FILE(module, "Sending to frontend3");
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);
                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::CancelAck:
                    {

                        const uint32_t oms_order_id = msg.cancel_ack.oms_order_id;
                        // LOG_COUT("Cancelled oms id:" + std::to_string(oms_order_id));
                        LOG_FILE(module, "Cancelled oms id:" + std::to_string(oms_order_id));
                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        leg.order_state = OrderState::CancelExchange;

                        const uint16_t portfolio_id = leg.portfolio_id;
                        auto &orders = open_orders[portfolio_id];
                        orders.erase(std::remove(orders.begin(), orders.end(), oms_order_id), orders.end());

                        // Portfolio &strat = portfolios[portfolio_id];

                        // handleCancel(strat, oms_order_id, leg.required_qty, leg.fill_qty_sum, *order_manager, st);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;
                        //     update_msg.update.terminate = true;
                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 6");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);
                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::RmsReject:
                    {
                        LOG_FILE(module, "RmsReject oms id:" + std::to_string(msg.reject.oms_order_id));
                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        // LOG_COUT("RmsReject oms id:" + std::to_string(oms_order_id));
                        LOG_LIVE(module, "RmsReject oms id:" + std::to_string(oms_order_id));
                        StrategyLegData &leg = oms_to_leg[oms_order_id];

                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];

                        // strat.is_active = false;
                        // if (strat.is_iter_over)
                        // {
                        //     strat.stale_status = StaleStatus::NOT_STALE;
                        //     update_msg.msg_type = FrontendMessageType::Status;
                        //     update_msg.portfolio_id = portfolio_id;
                        //     update_msg.update.is_active = false;
                        //     update_msg.update.terminate = true;
                        //     update_msg.update.update_reason = UpdateReason::RMSReject;
                        // }
                        // else
                        // {
                        //     strat.stale_status = StaleStatus::RMS_REJECTED;
                        //     FrontendMessage stale_status_update{};
                        //     stale_status_update.msg_type = FrontendMessageType::StaleStatusUpdate; // FORCESTOPSTALE, RMSStale
                        //     stale_status_update.portfolio_id = portfolio_id;
                        //     stale_status_update.stale_status_update.status = StaleStatus::RMS_REJECTED;
                        //     if (socketManager.getFrontendClientSocket() != -1)
                        //     {
                        //         LOG_FILE(module, "Sending Force Stop stale status");
                        //         send(socketManager.getFrontendClientSocket(), &stale_status_update, sizeof(stale_status_update), MSG_DONTWAIT);
                        //     }
                        // }

                        // push_portfolio(strat);
                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        push_oms_data(leg);

                        break;
                    }

                    case OrderMessageType::ModifyReject:
                    {
                        LOG_FILE(module, "ModifyReject oms id:" + std::to_string(msg.update.oms_order_id));
                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        // LOG_COUT("ModifyReject oms id:" + std::to_string(oms_order_id));
                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        oms_to_leg[oms_order_id].required_qty = old_oms_to_leg[oms_order_id].required_qty;
                        const uint16_t portfolio_id = leg.portfolio_id;
                        // Portfolio &strat = portfolios[portfolio_id];

                        // handleModifyReject(strat, leg.oms_order_id, leg.required_qty);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;

                        //     update_msg.update.terminate = true;

                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 7");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::CancelReject:
                    {
                        LOG_FILE(module, "CancelReject oms id:" + std::to_string(msg.update.oms_order_id));

                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        // LOG_COUT("CancelReject oms id:" + std::to_string(oms_order_id));
                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        const uint16_t portfolio_id = leg.portfolio_id;
                        // Portfolio &strat = portfolios[portfolio_id];

                        // handleCancelReject(strat, leg.oms_order_id, leg.required_qty);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {

                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;

                        //     update_msg.update.terminate = true;

                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 8");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    case OrderMessageType::RequestFailed:
                    {
                        // LOG_FILE(module, "RequestFailed oms id:" + std::to_string(msg.reject.oms_order_id));
                        // LOG_LIVE(module, "RequestFailed oms id:" + std::to_string(msg.reject.oms_order_id));
                        // const uint32_t oms_order_id = msg.reject.oms_order_id;
                        // LOG_COUT("RequestFailed oms id:" + std::to_string(oms_order_id));
                        // StrategyLegData &leg = oms_to_leg[oms_order_id];

                        // const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];
                        // handleRequestFailed(strat, leg.oms_order_id, leg.required_qty, leg.fill_qty_sum, *order_manager, st);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;

                        //     update_msg.update.terminate = true;

                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 9");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }
                        // push_oms_data(leg);
                        // push_order_data(strat);

                        LOG_FILE(module, "RmsReject in Request Failed oms id:" + std::to_string(msg.reject.oms_order_id));
                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        LOG_COUT("RmsReject oms id in Request Failed:" + std::to_string(oms_order_id));
                        LOG_LIVE(module, "RmsReject oms id in Request Failed:" + std::to_string(oms_order_id));
                        StrategyLegData &leg = oms_to_leg[oms_order_id];

                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];
                        // strat.stop_reason = UpdateReason::RequestFailed;

                        // strat.is_active = false;
                        // if (strat.is_iter_over)
                        // {
                        //     strat.stale_status = StaleStatus::NOT_STALE;
                        //     update_msg.msg_type = FrontendMessageType::Status;
                        //     update_msg.portfolio_id = portfolio_id;
                        //     update_msg.update.is_active = false;
                        //     update_msg.update.terminate = true;
                        //     update_msg.update.update_reason = UpdateReason::RMSReject;
                        // }
                        // else
                        // {
                        //     strat.stale_status = StaleStatus::RMS_REJECTED;
                        //     FrontendMessage stale_status_update{};
                        //     stale_status_update.msg_type = FrontendMessageType::StaleStatusUpdate; // FORCESTOPSTALE, RMSStale
                        //     stale_status_update.portfolio_id = portfolio_id;
                        //     stale_status_update.stale_status_update.status = StaleStatus::RMS_REJECTED;
                        //     if (socketManager.getFrontendClientSocket() != -1)
                        //     {
                        //         LOG_FILE(module, "Sending Force Stop stale status");
                        //         send(socketManager.getFrontendClientSocket(), &stale_status_update, sizeof(stale_status_update), MSG_DONTWAIT);
                        //     }
                        // }

                        // push_portfolio(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        push_oms_data(leg);

                        break;
                    }

                    case OrderMessageType::ModifyFailed:
                    {
                        LOG_FILE(module, "RmsReject in Modify Failed oms id sending stop strategy:" + std::to_string(msg.reject.oms_order_id));
                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        LOG_COUT("RmsReject oms id in Modify Failed oms id sending stop strategy:" + std::to_string(oms_order_id));
                        LOG_LIVE(module, "RmsReject oms id in Modify Failed oms id sending stop strategy:" + std::to_string(oms_order_id));

                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        oms_to_leg[oms_order_id].required_qty = old_oms_to_leg[oms_order_id].required_qty;
                        const uint16_t portfolio_id = leg.portfolio_id;
                        // Portfolio &strat = portfolios[portfolio_id];
                        // strat.stop_requested = true; // [[IMPORTANT STOPPING STRATEGY]]
                        // strat.stop_reason = UpdateReason::RMSReject;
                       

                        // strat.is_active = false;
                        // if (strat.is_iter_over)
                        // {
                        //     strat.stale_status = StaleStatus::NOT_STALE;
                        //     update_msg.msg_type = FrontendMessageType::Status;
                        //     update_msg.portfolio_id = portfolio_id;
                        //     update_msg.update.is_active = false;
                        //     update_msg.update.terminate = false;
                        //     update_msg.update.update_reason = UpdateReason::RMSReject;
                        // }
                        // else
                        // {
                        //     strat.stale_status = StaleStatus::RMS_REJECTED;
                        //     FrontendMessage stale_status_update{};
                        //     stale_status_update.msg_type = FrontendMessageType::StaleStatusUpdate; // FORCESTOPSTALE, RMSStale
                        //     stale_status_update.portfolio_id = portfolio_id;
                        //     stale_status_update.stale_status_update.status = StaleStatus::RMS_REJECTED;
                        //     if (socketManager.getFrontendClientSocket() != -1)
                        //     {
                        //         LOG_FILE(module, "Sending Force Stop stale status");
                        //         send(socketManager.getFrontendClientSocket(), &stale_status_update, sizeof(stale_status_update), MSG_DONTWAIT);
                        //     }
                        // }
                        /////////////////////////
                        // CHANGING MODIFY FAILED TO STOP STRATEGY 
                        ///////////////////////////

                        // handleModifyReject(strat, leg.oms_order_id, leg.required_qty);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {
                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;

                        //     update_msg.update.terminate = true;

                        //     if (strat.terminate)
                        //     {
                        //         LOG_FILE(module, "Strategy Complete 7");

                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }

                        // push_portfolio(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        push_oms_data(leg);

                        break;
                    }

                    case OrderMessageType::CancelFailed:
                    {
                        LOG_FILE(module, "CancelFailed oms id:" + std::to_string(msg.update.oms_order_id));
                        const uint32_t oms_order_id = msg.reject.oms_order_id;
                        LOG_COUT("CancelFailed oms id:" + std::to_string(oms_order_id));
                        LOG_LIVE(module, "CancelFailed oms id in Modify Failed:" + std::to_string(oms_order_id));
                        StrategyLegData &leg = oms_to_leg[oms_order_id];
                        const uint16_t portfolio_id = leg.portfolio_id;

                        // Portfolio &strat = portfolios[portfolio_id];

                        // strat.stop_requested = true; // [[IMPORTANT STOPPING STRATEGY]]
                        // strat.stop_reason = UpdateReason::RMSReject;
                        // handleCancelReject(strat, leg.oms_order_id, leg.required_qty);

                        // if (isStrategyComplete(strat)) [[unlikely]]
                        // {

                        //     update_msg.msg_type = FrontendMessageType::Status;

                        //     update_msg.portfolio_id = portfolio_id;

                        //     update_msg.update.terminate = true;

                        //     if (strat.terminate)
                        //     {
                        //         update_msg.update.update_reason = UpdateReason::Completed;
                        //     }
                        //     else
                        //     {
                        //         update_msg.update.update_reason = strat.stop_reason;
                        //     }
                        //     push_portfolio(strat);
                        // }
                        push_oms_data(leg);
                        // push_order_data(strat);

                        // After handling Fill, PartialFill, NewOrderAck, etc.
// ADD this to call strategy callback:

PortfolioSlot& slot = portfolio_slots[portfolio_id];
if (slot.allocated && slot.fn_table.on_order_update)
{
    OrderUpdate upd;
    upd.oms_order_id = oms_order_id;
    upd.exchange_order_id = leg.exchange_order_id;
    upd.token = leg.token;
    upd.side = (uint8_t)leg.side;
    upd.state = leg.order_state;
    upd.ordered_price = leg.ordered_price;
    upd.ordered_qty = leg.required_qty;
    upd.filled_qty = leg.fill_qty_sum;
    upd.avg_fill_price = leg.fill_qty_sum > 0 ? 
                         leg.fill_price_sum / leg.fill_qty_sum : 0;
    
    slot.fn_table.on_order_update(
        slot.strategy_handle,
        (PlatformContext*)this,
        portfolio_id,
        &upd);
}

                        break;
                    }

                    default:
                    {
                        LOG_FILE(module, "NOT HANDLED CASE :" + std::to_string(static_cast<int>(msg.type)));
                        LOG_LIVE(module, "NOT HANDLED CASE :" + std::to_string(static_cast<int>(msg.type)));
                    }
                    }

                    // Send update to frontend
                    update_msg.update.oms_msg = msg;
                    LOG_FILE(module, "Sending OMS update to frontend: " +
                                         std::to_string(socketManager.getFrontendClientSocket()));

                    if (socketManager.getFrontendClientSocket() != -1)
                    {
                        send(socketManager.getFrontendClientSocket(), &update_msg,
                             sizeof(update_msg), MSG_DONTWAIT);
                    }
                }

                // Log buffer utilization if it's getting high
                if (utrade_rx_buffer.utilization() > 80.0f) [[unlikely]]
                {
                    LOG_FILE(module, "WARNING: UTrade buffer utilization at " +
                                         std::to_string(utrade_rx_buffer.utilization()) + "%");
                }

                break;
            }

            case FDTag::Market:
            {

                while (true)
                {
                    MarketData data;
                    struct sockaddr_in src_addr;
                    socklen_t addrlen = sizeof(src_addr);

                    ssize_t nbytes = recvfrom(socketManager.getMarketSocket(),
                                              &data, sizeof(MarketData), 0,
                                              (sockaddr *)&src_addr, &addrlen);
                    auto st = __rdtsc();
                    auto now = Clock::now();

                    if (nbytes != sizeof(MarketData)) [[unlikely]]
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break; // No more data - normal exit
                        }
                        if (nbytes == 0)
                        {
                            std::cerr << "Marketdata Socket Disconnected\n";
                            break;
                        }
                        continue; // Drop malformed packets
                    }

                    TimePoint& last = stream_to_sequence[data.stream_id].last_time;
                    uint32_t& last_seq = stream_to_sequence[data.stream_id].seq_no;

                    auto diff = now - last;
                    auto diff_sec = std::chrono::duration_cast<std::chrono::seconds>(diff).count();

                    


                    if (data.internal_seqno - last_seq != 1   ||  (diff_sec >30) ) [[unlikely]]
                    {
                        std::cout << module
                            << " Sequence Mismatch for stream "
                            << static_cast<int>(data.stream_id)
                            << " last seq: " << last_seq
                            << " current seq: " << data.internal_seqno
                            << " token: " << data.token
                            << std::endl;

                        std::cout << module
                                << " Delay detected for stream "
                                << static_cast<int>(data.stream_id)
                                << " delay: " << diff_sec << " sec"
                                << " token: " << data.token
                                << std::endl;


                        if((diff_sec >30)) [[unlikely]]
                        {
                            // stopping all portfolio which has tick stale
                            std::unordered_set<uint16_t> portfolio_ids = stream_to_sequence[data.stream_id].portfolio_ids;
                            for(auto portfolio_id : portfolio_ids)
                            {
                                std::cout<<"Stopping porfolio id due to tick stale :" << portfolio_id<<std::endl;
                                // Portfolio &strat = portfolios[portfolio_id]; 
                                // strat.stop_requested = true;
                                // strat.stop_reason = UpdateReason::TickStale;
                            }
                        }
                    }

                    last = now;
                    last_seq = data.internal_seqno;


                    

                   
                    StoredMarketDataLatency &stored = orderbook[data.token];
                    
                    std::memcpy(&stored.bids[0], &data.bids[0], 80);
                    stored.seqno = data.seqno;
                    stored.msg_type = data.msg_type;
                    stored.internal_seqno = data.internal_seqno;
                    stored.stream_id = data.stream_id;
                    stored.last_traded_price = data.last_traded_price;
                    stored.start_time = data.timestamp;
                   
                    auto portfolio_it = token_to_portfolios.find(data.token);

                    if (portfolio_it != token_to_portfolios.end()) [[likely]]
                    {
                        for (auto portfolio_id : portfolio_it->second)
                        {

                            PortfolioSlot& slot = portfolio_slots[portfolio_id];
                            if (!slot.allocated || !slot.fn_table.on_market_event) [[unlikely]]
                            {
                                continue;
                            }

                            // Build MarketEvent from your StoredMarketDataLatency
                            MarketEvent ev;
                            ev.token = data.token;
                            std::memcpy(&ev.bids[0], &data.bids[0], 80);
                            ev.seqno = data.seqno;
                            ev.msg_type = data.msg_type;
                            ev.internal_seqno = data.internal_seqno;
                            ev.stream_id = data.stream_id;
                            ev.last_traded_price = data.last_traded_price;
                            ev.start_time = data.timestamp;

                            // HOT PATH: Direct function pointer call to .so
                            slot.fn_table.on_market_event(
                                slot.strategy_handle,
                                (PlatformContext*)this,
                                portfolio_id,
                                &ev);
                            
                            // Portfolio &strat = portfolios[portfolio_id];
                            // if (!strat.is_active) [[unlikely]]
                            // {
                            //     continue;
                            // }

                            // LOG_FILE(module, "Pf id executing :" + std::to_string(portfolio_id));

                            // // switch (strat.kind)
                            // // {
                            // // case StrategyKind::CONREV_IOC:
                            // // {
                            // //     if (data.msg_type == 'T' || data.msg_type == 'X') [[unlikely]]
                            // //     {
                            // //         continue;
                            // //     }

                            // //     const uint32_t fut_token = strat.legs[1].symbol_token;
                            // //     const uint32_t call_token = strat.legs[1].symbol_token;
                            // //     const uint32_t put_token = strat.legs[2].symbol_token;
                            // //     auto fut_it = orderbook.find(fut_token);
                            // //     auto call_it = orderbook.find(call_token);
                            // //     auto put_it = orderbook.find(put_token);

                            // //     if (fut_it == orderbook.end() || call_it == orderbook.end() || put_it == orderbook.end()) [[unlikely]]
                            // //     {
                            // //         LOG_LIVE(module, "Data of call or put not there in orderbook");
                            // //         continue;
                            // //     }

                            // //     StrategyMarketSnapshot snap;
                            // //     snap.kind = StrategyKind::CONREV_IOC;
                            // //     snap.is_valid = true;

                            // //     snap.data.conrev.fut = fut_it->second;
                            // //     snap.data.conrev.call = call_it->second;
                            // //     snap.data.conrev.put = put_it->second;

                            // //     strat.is_iter_over = false;
                               
                            // //     bool sent = executeStrategy(strat, snap, *order_manager, st);
                            // //     strat.is_iter_over = true;

                            // //     LOG_FILE(module, std::to_string(sent) + " , " + std::to_string(isStrategyComplete(strat)));

                            // //     if (sent && isStrategyComplete(strat)) [[unlikely]]
                            // //     {
                            // //         LOG_FILE(module, "ConRev strategy completed. Removing from active list.");
                            // //         FrontendMessage update_msg{};
                            // //         update_msg.msg_type = FrontendMessageType::Status;
                            // //         update_msg.portfolio_id = portfolio_id;
                            // //         update_msg.update.terminate = true;

                            // //         if (strat.terminate)
                            // //         {
                            // //             LOG_FILE(module, "Strategy Complete 12");
                            // //             update_msg.update.update_reason = UpdateReason::Completed;
                            // //         }
                            // //         else
                            // //         {
                            // //             update_msg.update.update_reason = strat.stop_reason; 
                            // //         }
                            // //         LOG_FILE(module, "Sending to frontend9");

                            // //         if (socketManager.getFrontendClientSocket() != -1)
                            // //         {
                            // //             send(socketManager.getFrontendClientSocket(), &update_msg, sizeof(update_msg), MSG_DONTWAIT);
                            // //         }
                            // //     }

                            // //     break;
                            // // }

                            // // default:
                            // //     [[unlikely]] LOG_LIVE(module, "Unknown strategy type: " + std::to_string(static_cast<int>(strat.kind)));
                            // //     break;
                            // // }

                            // push_order_data(strat); // pushing data to db
                            // push_portfolio(strat);
                            //////////////////////////////////////////////////////////

                        }
                    }
                   
                   
                   
                }
                break;
            }

            default:
                std::cerr << "Unknown FDTag in epoll event";
                break;
            }
        }

        _mm_pause();
    }
}

void HFTStrategyEngine::handle_frontend_event()
{
    // ============================================================
    // STEP 1: Receive all available data into ring buffer
    // ============================================================
    while (true)
    {
        size_t space = frontend_rx_buffer.contiguous_write_space();

        if (space == 0) [[unlikely]]
        {
            LOG_FILE(module, "CRITICAL: Frontend RX buffer full");
            LOG_LIVE(module, "CRITICAL: Frontend RX buffer full");
            break;
        }

        ssize_t bytes = recv(
            socketManager.getFrontendClientSocket(),
            frontend_rx_buffer.write_ptr(),
            space,
            0);

        if (bytes < 0) [[unlikely]]
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            else
            {
                perror("recv frontend");
                LOG_FILE(module, "Frontend recv error: " + std::string(strerror(errno)));
                break;
            }
        }

        if (bytes == 0) [[unlikely]]
        {
            LOG_COUT("Frontend Disconnected\n");
            LOG_FILE(module, "Frontend Disconnected");
            
            // Stop all active portfolios
            for (int i = 0; i < MAX_PORTFOLIOS; i++)
            {
                if (portfolio_slots[i].allocated && portfolio_slots[i].fn_table.on_stop)
                {
                    uint8_t dummy_resp[256];
                    uint32_t dummy_len;
                    portfolio_slots[i].fn_table.on_stop(
                        portfolio_slots[i].strategy_handle,
                        (PlatformContext*)this,
                        i,
                        dummy_resp,
                        &dummy_len);
                }
            }

            socketManager.setFrontendClientSocket(-1);
            frontend_rx_buffer.reset();
            return;
        }

        frontend_rx_buffer.commit_write(bytes);
    }

    // ============================================================
    // STEP 2: Process all complete messages
    // ============================================================
    while (frontend_rx_buffer.available() >= sizeof(FrontendCommand))
    {
        FrontendCommand cmd;
        const FrontendCommand* cmd_ptr = frontend_rx_buffer.peek();

        if (cmd_ptr) [[likely]]
        {
            cmd = *cmd_ptr;
        }
        else [[unlikely]]
        {
            if (!frontend_rx_buffer.read_wrapped(cmd))
            {
                LOG_FILE(module, "Incomplete message in buffer");
                break;
            }
        }

        frontend_rx_buffer.consume(sizeof(FrontendCommand));

        LOG_FILE(module, "Processing frontend command: type=" + 
                 std::to_string((int)cmd.cmd_type) + " pf_id=" + std::to_string(cmd.pf_id));

        // Prepare response
        FrontendResponse resp;
        resp.pf_id = cmd.pf_id;
        resp.status = 0;
        resp.response_len = 0;

        uint32_t pf_id = cmd.pf_id;
        PortfolioSlot& slot = portfolio_slots[pf_id];

        switch (cmd.cmd_type)
        {
        case FRONTEND_CMD_ADD:
        {
            // Extract type_id from payload (first 4 bytes)
            if (cmd.payload_len < sizeof(uint32_t)) {
                resp.status = -1;
                const char* err = "Invalid payload";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                break;
            }
            
            uint32_t type_id;
            memcpy(&type_id, cmd.payload, sizeof(uint32_t));
            
            StrategyPlugin* plugin = find_plugin_by_type(type_id);
            if (!plugin) {
                resp.status = -1;
                const char* err = "Type ID not found";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                
                LOG_FILE(module, "Type ID not found: " + std::to_string(type_id));
                break;
            }
            
            // Create strategy instance
            StrategyFnTable fn_table;
            void* handle = plugin->create(&fn_table);
            if (!handle) {
                resp.status = -1;
                const char* err = "Strategy creation failed";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                
                LOG_FILE(module, "Strategy creation failed");
                break;
            }
            
            // Store in slot
            slot.plugin = plugin;
            slot.strategy_handle = handle;
            slot.fn_table = fn_table;
            slot.allocated = true;
            
            // Call on_add (params start after type_id)
            const uint8_t* params = cmd.payload + sizeof(uint32_t);
            uint32_t params_len = cmd.payload_len - sizeof(uint32_t);
            
            resp.status = fn_table.on_add(
                handle,
                (PlatformContext*)this,
                &s_platform_api,
                pf_id,
                params,
                params_len,
                resp.response,
                &resp.response_len);
            
            LOG_FILE(module, "Strategy added: pf_id=" + std::to_string(pf_id) +
                     " type_id=" + std::to_string(type_id));
            
            break;
        }

        case FRONTEND_CMD_EDIT:
        {
            if (!slot.allocated) {
                resp.status = -1;
                const char* err = "Portfolio not found";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                break;
            }
            
            resp.status = slot.fn_table.on_edit(
                slot.strategy_handle,
                (PlatformContext*)this,
                pf_id,
                cmd.payload,
                cmd.payload_len,
                resp.response,
                &resp.response_len);
            
            LOG_FILE(module, "Strategy edited: pf_id=" + std::to_string(pf_id));
            
            break;
        }

        case FRONTEND_CMD_RUN:
        {
            if (!slot.allocated) {
                resp.status = -1;
                const char* err = "Portfolio not found";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                break;
            }
            
            resp.status = slot.fn_table.on_run(
                slot.strategy_handle,
                (PlatformContext*)this,
                pf_id,
                resp.response,
                &resp.response_len);
            
            LOG_FILE(module, "Strategy running: pf_id=" + std::to_string(pf_id));
            
            break;
        }

        case FRONTEND_CMD_STOP:
        {
            if (!slot.allocated) {
                resp.status = -1;
                const char* err = "Portfolio not found";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                break;
            }
            
            resp.status = slot.fn_table.on_stop(
                slot.strategy_handle,
                (PlatformContext*)this,
                pf_id,
                resp.response,
                &resp.response_len);
            
            LOG_FILE(module, "Strategy stopped: pf_id=" + std::to_string(pf_id));
            
            break;
        }

        case FRONTEND_CMD_REMOVE:
        {
            if (!slot.allocated) {
                resp.status = -1;
                const char* err = "Portfolio not found";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                break;
            }
            
            resp.status = slot.fn_table.on_remove(
                slot.strategy_handle,
                (PlatformContext*)this,
                pf_id,
                resp.response,
                &resp.response_len);
            
            slot.allocated = false;
            slot.strategy_handle = nullptr;
            slot.plugin = nullptr;
            
            LOG_FILE(module, "Strategy removed: pf_id=" + std::to_string(pf_id));
            
            break;
        }

        case FRONTEND_CMD_QUERY:
        {
            if (!slot.allocated) {
                resp.status = -1;
                const char* err = "Portfolio not found";
                memcpy(resp.response, err, strlen(err));
                resp.response_len = strlen(err);
                break;
            }
            
            resp.status = slot.fn_table.on_query(
                slot.strategy_handle,
                (PlatformContext*)this,
                pf_id,
                resp.response,
                &resp.response_len);
            
            break;
        }

        default:
            resp.status = -1;
            const char* err = "Unknown command";
            memcpy(resp.response, err, strlen(err));
            resp.response_len = strlen(err);
            break;
        }

        // Send response
        if (socketManager.getFrontendClientSocket() != -1)
        {
            send(socketManager.getFrontendClientSocket(), &resp,
                 sizeof(FrontendResponse), MSG_DONTWAIT);
        }
    }

    if (frontend_rx_buffer.utilization() > 80.0f) [[unlikely]]
    {
        LOG_FILE(module, "WARNING: Frontend buffer utilization at " +
                         std::to_string(frontend_rx_buffer.utilization()) + "%");
    }
}
// // To handle Frontend Events
// void HFTStrategyEngine::handle_frontend_event()
// {

//     // ============================================================
//     // STEP 1: Receive all available data into ring buffer
//     // ============================================================
//     while (true)
//     {
//         size_t space = frontend_rx_buffer.contiguous_write_space();

//         if (space == 0) [[unlikely]]
//         {
//             LOG_FILE(module, "CRITICAL: Frontend RX buffer full");
//             LOG_LIVE(module, "CRITICAL: Frontend RX buffer full - possible protocol error");
//             // Buffer full shouldn't happen in normal operation
//             // You might want to reset connection here
//             break;
//         }

//         ssize_t bytes = recv(
//             socketManager.getFrontendClientSocket(),
//             frontend_rx_buffer.write_ptr(),
//             space,
//             0 // Non-blocking socket, no MSG_DONTWAIT needed
//         );

//         if (bytes < 0) [[unlikely]]
//         {
//             if (errno == EAGAIN || errno == EWOULDBLOCK)
//             {
//                 // No more data available - this is NORMAL
//                 break;
//             }
//             else
//             {
//                 perror("recv frontend");
//                 LOG_FILE(module, "Frontend recv error: " + std::string(strerror(errno)));
//                 break;
//             }
//         }

//         if (bytes == 0) [[unlikely]]
//         {
//             // Frontend disconnected
//             LOG_COUT("Frontend Disconnected\n");
//             LOG_FILE(module, "Frontend Disconnected");
//             LOG_LIVE(module, "Frontend Disconnected");

//             // Stop all active portfolios
//             for (int i = 0; i < MAX_PORTFOLIOS; i++)
//             {
//                 if (portfolio_initialized[i])
//                 {
//                     LOG_FILE(module, "Doing Stop for pf id:" + std::to_string(i));
//                     auto &portfolio = portfolios[i];
//                     if (!portfolio.is_active)
//                         continue;

//                     if (portfolio.is_iter_over)
//                     {
//                         portfolio.is_active = false;
//                     }
//                     else
//                     {
//                         portfolio.stop_requested = true;
//                     }
//                     push_portfolio(portfolio);
//                 }
//             }

//             socketManager.setFrontendClientSocket(-1);
//             LOG_FILE(module, "Set frontend client socket as -1: " +
//                                  std::to_string(socketManager.getFrontendClientSocket()));
//             LOG_LIVE(module, "Set frontend client socket as -1: " +
//                                  std::to_string(socketManager.getFrontendClientSocket()));

//             // Reset buffer for clean reconnection
//             frontend_rx_buffer.reset();
//             return;
//         }

//         // Successfully received bytes - commit to buffer
//         frontend_rx_buffer.commit_write(bytes);

//         LOG_FILE(module, "Received " + std::to_string(bytes) +
//                              " bytes from frontend, buffer has " +
//                              std::to_string(frontend_rx_buffer.available()) + " bytes");
//     }

//     // ============================================================
//     // STEP 2: Process all complete messages (ZERO copies!)
//     // ============================================================
//     while (frontend_rx_buffer.available() >= sizeof(FrontendMessage))
//     {
//         FrontendMessage msg;

//         // Try zero-copy peek first (common case - no wrap)
//         const FrontendMessage *msg_ptr = frontend_rx_buffer.peek();

//         if (msg_ptr) [[likely]]
//         {
//             // FAST PATH: Direct pointer access, zero copy!
//             msg = *msg_ptr;
//         }
//         else [[unlikely]]
//         {
//             // SLOW PATH: Message spans wrap point
//             if (!frontend_rx_buffer.read_wrapped(msg))
//             {
//                 // Incomplete message, wait for more data
//                 LOG_FILE(module, "Incomplete message in buffer, waiting for more data");
//                 break;
//             }
//         }

//         // Message is complete and valid - consume it from buffer
//         frontend_rx_buffer.consume(sizeof(FrontendMessage));

//         // Log the received message
//         LOG_FILE(module, "Processing message type: " +
//                              std::to_string(static_cast<int>(msg.msg_type)));

//         // Push to logs
//         push_logs(false, uint8_t(msg.msg_type));

//         switch (msg.msg_type)
//         {

//         case FrontendMessageType::Run:
//         {
//             uint16_t pf_id = msg.portfolio_id;

//             LOG_COUT("Run came from fs :" << pf_id);
//             LOG_LIVE(module,"Run came from fs :" + std::to_string(pf_id));
//             // last_pf_id++;
//             // if(last_pf_id!=pf_id)
//             // {
//             //     LOG_COUT
//             // }

//             // Check if already exists
//             bool already_exists = portfolio_initialized[pf_id];
//             Portfolio &s = portfolios[pf_id];

//             if (!already_exists)
//             {
//                 s = Portfolio(); // Only zero/init on first creation
//                 s.portfolio_id = pf_id;
//                 s.kind = msg.add.strategy_kind;
//                 s.leg_count = msg.add.leg_count;
//             }
//             else
//             {
//                 LOG_FILE(module, "Portfolio already exists. Updating parameters only.");
//                 s.kind = msg.add.strategy_kind;
//                 s.leg_count = msg.add.leg_count;
//             }

//             std::memcpy(s.legs, msg.add.legs, sizeof(s.legs));

//             for (int i = 0; i < s.leg_count; i++)
//             {

//                 token_to_portfolios[s.legs[i].symbol_token].insert(pf_id);
//                 stream_to_sequence[s.legs[i].stream].portfolio_ids.insert(pf_id);
//                 LOG_FILE(module, "Token coming from frontend: " + std::to_string(msg.add.legs[i].symbol_token) + " For pf id:" + std::to_string(pf_id) + " With stream:" + std::to_string(static_cast<int>(s.legs[i].stream)));
//             }

            

//             s.updated_tick = false;
//             s.is_active = true;
//             s.terminate = false;
//             s.stop_requested = false;
//             s.is_iter_over = true;

//             switch (s.kind)
//             {
//             case StrategyKind::CONREV_IOC:
//             {

//                 auto &conrev_params = s.params.conrev;
//                 conrev_params.portfolio_id = pf_id;
//                 conrev_params.max_lots = msg.add.params.CONREV_IOC.max_lots;
//                 LOG_FILE(module, "sol:" + std::to_string(msg.add.params.CONREV_IOC.sol) + ", maxlots" + std::to_string(msg.add.params.CONREV_IOC.max_lots));
//                 conrev_params.sol = msg.add.params.CONREV_IOC.sol;
//                 conrev_params.con_flag = msg.add.params.CONREV_IOC.con_flag;
//                 LOG_FILE(module, "con flag: " + std::to_string(msg.add.params.CONREV_IOC.con_flag));

//                 conrev_params.spread = msg.add.params.CONREV_IOC.spread;

//                 LOG_FILE(module,
//                          "ConRev Order Data → Portfolio ID: " + std::to_string(pf_id) +
//                              ", Fut Token: " + std::to_string(s.legs[0].symbol_token) +
//                              ", Call Token: " + std::to_string(s.legs[1].symbol_token) +
//                              ", Put Token: " + std::to_string(s.legs[2].symbol_token) +
//                              ", Strike Price: " + std::to_string(s.legs[1].strike_price));

//                 s.order_data.conrev.portfolio_id = pf_id;
//                 s.order_data.conrev.fut_token = s.legs[0].symbol_token;
//                 s.order_data.conrev.call_token = s.legs[1].symbol_token;
//                 s.order_data.conrev.put_token = s.legs[2].symbol_token;
//                 s.order_data.conrev.strike_price = s.legs[1].strike_price;

//                 break;
//             }
//             case StrategyKind::CONREV_BID:
//             {

//                 auto &bidding_params = s.params.three_leg_bidding;
//                 bidding_params.portfolio_id = pf_id;
//                 auto &bidding_order_data = s.order_data.three_leg_bidding;
//                 bidding_order_data.portfolio_id = pf_id;
//                 bidding_params.max_lots = msg.add.params.CONREV_BID.max_lots;
//                 bidding_params.sol = msg.add.params.CONREV_BID.sol;
//                 bidding_params.con_flag = msg.add.params.CONREV_BID.con_flag;
//                 bidding_params.spread = msg.add.params.CONREV_BID.spread;

//                 bidding_params.is_opportunity = msg.add.params.CONREV_BID.opp_check;
//                 bidding_params.leg1_spread_threshold = msg.add.params.CONREV_BID.diff;
//                 bidding_params.legs2_timeout_us = msg.add.params.CONREV_BID.timer;
//                 bidding_params.legs3_timeout_us = msg.add.params.CONREV_BID.timer;
//                 if (!already_exists)
//                 {
//                     bidding_order_data.achieved_spread = 0;
//                     bidding_order_data.traded_qty = 0;
//                 }

//                 bidding_order_data.strike_price =
//                     (s.legs[0].leg_type == LegType::Call || s.legs[0].leg_type == LegType::Put)
//                         ? s.legs[0].strike_price
//                         : s.legs[1].strike_price;
//                 bidding_order_data.new_max = 0;
//                 bidding_order_data.limit_new_max = 200000;
//                 bidding_order_data.mod_max = 0;
//                 bidding_order_data.limit_mod_max = 200000;
//                 bidding_order_data.state = ThreeLegBiddingState::IDLE;

//                 LOG_FILE(module, "traded qty:" + std::to_string(s.order_data.three_leg_bidding.traded_qty));
//                 LOG_FILE(module, "max lots:" + std::to_string(bidding_params.max_lots));
//                 LOG_FILE(module, " sol:" + std::to_string(bidding_params.sol));

//                 LOG_FILE(module,
//                          "ConRev Bidding Order Data → Portfolio ID: " + std::to_string(pf_id) +
//                              ", Token1: " + std::to_string(s.legs[0].symbol_token) +
//                              ", strike_price: " + std::to_string(s.legs[0].strike_price) +
//                              ", strike_price: " + std::to_string(s.legs[1].strike_price) +
//                              ", strike_price: " + std::to_string(s.legs[2].strike_price) +
//                              ", type: " + std::to_string(static_cast<int>(s.legs[0].leg_type)) +
//                              ", Token2: " + std::to_string(s.legs[1].symbol_token) +
//                              ", type: " + std::to_string(static_cast<int>(s.legs[1].leg_type)) +
//                              ", Token3: " + std::to_string(s.legs[2].symbol_token) +
//                              ", type: " + std::to_string(static_cast<int>(s.legs[2].leg_type)) +
//                              ", Strike Price: " + std::to_string(bidding_order_data.strike_price));
//                 break;
//             }
//             case StrategyKind::BOX_1_1_1_1:
//             {

//                 LOG_FILE(module, "msg.add.params.BOX_1_1_1_1.max_lots: " + std::to_string(msg.add.params.BOX_1_1_1_1.maxLots) +
//                                      " msg.add.params.BOX_1_1_1_1.sol: " + std::to_string(msg.add.params.BOX_1_1_1_1.sol) +
//                                      " msg.add.params.BOX_1_1_1_1.entry_leg: " + std::to_string(msg.add.params.BOX_1_1_1_1.entry_leg) +
//                                      " msg.add.params.BOX_1_1_1_1.TimeoutUs: " + std::to_string(msg.add.params.BOX_1_1_1_1.TimeoutUs) +
//                                      " msg.add.params.BOX_1_1_1_1.price_difference: " + std::to_string(msg.add.params.BOX_1_1_1_1.priceDifference) +
//                                      " msg.add.params.BOX_1_1_1_1.leg1SpreadThreshold: " + std::to_string(msg.add.params.BOX_1_1_1_1.leg1SpreadThreshold) +
//                                      " msg.add.params.BOX_1_1_1_1.flipBoxEnabled: " + std::to_string(msg.add.params.BOX_1_1_1_1.flipBoxEnabled));

//                 LOG_FILE(module, "Box Bidding Run came from frontend");
//                 auto &bidding_params = s.params.box_bidding;
//                 bidding_params.portfolio_id = pf_id;
//                 bidding_params.max_lots = msg.add.params.BOX_1_1_1_1.maxLots;
//                 bidding_params.sol = msg.add.params.BOX_1_1_1_1.sol;
//                 bidding_params.flip_box_enabled = msg.add.params.BOX_1_1_1_1.flipBoxEnabled;
//                 bidding_params.price_difference = msg.add.params.BOX_1_1_1_1.priceDifference;
//                 if (bidding_params.flip_box_enabled)
//                     bidding_params.flip_price_difference = msg.add.params.BOX_1_1_1_1.priceDifference;

//                 auto &order_data = s.order_data.box_bidding;
//                 order_data.portfolio_id = pf_id;

//                 // initializing tokens
//                 order_data.itm_call_token = msg.add.legs[0].symbol_token;
//                 order_data.otm_put_token = msg.add.legs[1].symbol_token;
//                 order_data.otm_call_token = msg.add.legs[2].symbol_token;
//                 order_data.itm_put_token = msg.add.legs[3].symbol_token;

//                 bidding_params.is_opportunity = msg.add.params.BOX_1_1_1_1.isOpportunity;
//                 bidding_params.leg1_spread_threshold = msg.add.params.BOX_1_1_1_1.leg1SpreadThreshold;
//                 bidding_params.leg1_timeout_us = msg.add.params.BOX_1_1_1_1.TimeoutUs;
//                 bidding_params.legs2_timeout_us = msg.add.params.BOX_1_1_1_1.TimeoutUs;
//                 bidding_params.legs3_timeout_us = msg.add.params.BOX_1_1_1_1.TimeoutUs;
//                 bidding_params.legs4_timeout_us = msg.add.params.BOX_1_1_1_1.TimeoutUs;

//                 LOG_FILE(module, "leg1_timeout_us: " + std::to_string(bidding_params.leg1_timeout_us) + " " + std::to_string(bidding_params.leg1_timeout_us) + " " + std::to_string(bidding_params.leg1_timeout_us) + " " + std::to_string(bidding_params.leg1_timeout_us));

//                 bidding_params.entry_leg = msg.add.params.BOX_1_1_1_1.entry_leg;

//                 uint32_t strike_lower = -1, strike_higher = -1;
//                 LegData leg[4];
//                 for (auto itr : s.legs)
//                 {
//                     if (strike_lower == -1)
//                     {
//                         strike_lower = itr.strike_price;
//                     }
//                     else if (strike_lower != itr.strike_price)
//                     {
//                         strike_higher = itr.strike_price;
//                     }
//                 }

//                 if (strike_lower > strike_higher)
//                 {
//                     std::swap(strike_higher, strike_lower);
//                 }

//                 BoxBiddingLegs entry_leg;
//                 short entry_leg_count = 0;
//                 for (auto itr : s.legs)
//                 {
//                     entry_leg_count++;
//                     if (itr.strike_price == strike_lower)
//                     {
//                         if (itr.leg_type == LegType::Call)
//                         {
//                             if (entry_leg_count == bidding_params.entry_leg)
//                                 entry_leg = BoxBiddingLegs::ITM_CALL;
//                             order_data.itm_call_token = msg.add.legs[entry_leg_count - 1].symbol_token;
//                             leg[0] = itr;
//                         }
//                         else if (itr.leg_type == LegType::Put)
//                         {
//                             if (entry_leg_count == bidding_params.entry_leg)
//                                 entry_leg = BoxBiddingLegs::OTM_PUT;
//                             order_data.otm_put_token = msg.add.legs[entry_leg_count - 1].symbol_token;
//                             leg[1] = itr;
//                         }
//                         else
//                         {
//                             LOG_FILE("Run", "Wrong leg type while passing");
//                         }
//                     }
//                     else if (itr.strike_price == strike_higher)
//                     {
//                         if (itr.leg_type == LegType::Call)
//                         {
//                             if (entry_leg_count == bidding_params.entry_leg)
//                                 entry_leg = BoxBiddingLegs::OTM_CALL;
//                             order_data.otm_call_token = msg.add.legs[entry_leg_count - 1].symbol_token;
//                             leg[2] = itr;
//                         }
//                         else if (itr.leg_type == LegType::Put)
//                         {
//                             if (entry_leg_count == bidding_params.entry_leg)
//                                 entry_leg = BoxBiddingLegs::ITM_PUT;
//                             order_data.itm_put_token = msg.add.legs[entry_leg_count - 1].symbol_token;
//                             leg[3] = itr;
//                         }
//                         else
//                         {
//                             LOG_FILE("Run", "Wrong leg type while passing");
//                         }
//                     }
//                     else
//                     {
//                         LOG_FILE("Run", "Unknown strike price while parsing Box Bidding: " + std::to_string(itr.strike_price));
//                     }
//                 }

//                 memcpy(s.legs, leg, 4 * sizeof(LegData));

//                 switch (entry_leg)
//                 {
//                 case BoxBiddingLegs::ITM_CALL:
//                 {
//                     bidding_params.entry_leg = 1;
//                     order_data.entry_leg = 1;
//                     break;
//                 }
//                 case BoxBiddingLegs::OTM_PUT:
//                 {
//                     bidding_params.entry_leg = 2;
//                     order_data.entry_leg = 2;
//                     break;
//                 }
//                 case BoxBiddingLegs::OTM_CALL:
//                 {
//                     bidding_params.entry_leg = 3;
//                     order_data.entry_leg = 3;
//                     break;
//                 }
//                 case BoxBiddingLegs::ITM_PUT:
//                 {
//                     bidding_params.entry_leg = 4;
//                     order_data.entry_leg = 4;
//                     break;
//                 }
//                 default:
//                 {
//                     LOG_FILE(module, "Wrong BoxBiddingLeg while checking for entry leg in run for pfid: " + std::to_string(pf_id));
//                 }
//                 }

//                 // set strike price here only
//                 order_data.strike_diff = strike_higher - strike_lower;
//                 // LOG_COUT("strike_diff: " << order_data.strike_diff);

//                 switch (entry_leg)
//                 {
//                 case BoxBiddingLegs::ITM_CALL:
//                 {
//                     // LOG_COUT("Leg1 is entry leg");
//                     order_data.leg1 = BoxBiddingLegs::ITM_CALL;
//                     order_data.leg2 = BoxBiddingLegs::OTM_PUT;
//                     order_data.leg3 = BoxBiddingLegs::OTM_CALL;
//                     order_data.leg4 = BoxBiddingLegs::ITM_PUT;
//                     break;
//                 }
//                 case BoxBiddingLegs::OTM_PUT:
//                 {
//                     // LOG_COUT("Leg2 is entry leg");
//                     std::swap(s.legs[0], s.legs[1]);
//                     order_data.leg1 = BoxBiddingLegs::OTM_PUT;
//                     order_data.leg2 = BoxBiddingLegs::ITM_CALL;
//                     order_data.leg3 = BoxBiddingLegs::OTM_CALL;
//                     order_data.leg4 = BoxBiddingLegs::ITM_PUT;
//                     break;
//                 }
//                 case BoxBiddingLegs::OTM_CALL:
//                 {
//                     // LOG_COUT("Leg3 is entry leg");
//                     std::swap(s.legs[0], s.legs[2]);
//                     order_data.leg1 = BoxBiddingLegs::OTM_CALL;
//                     order_data.leg2 = BoxBiddingLegs::OTM_PUT;
//                     order_data.leg3 = BoxBiddingLegs::ITM_CALL;
//                     order_data.leg4 = BoxBiddingLegs::ITM_PUT;
//                     break;
//                 }
//                 case BoxBiddingLegs::ITM_PUT:
//                 {
//                     // LOG_COUT("Leg4 is entry leg");
//                     std::swap(s.legs[0], s.legs[3]);
//                     order_data.leg1 = BoxBiddingLegs::ITM_PUT;
//                     order_data.leg2 = BoxBiddingLegs::OTM_PUT;
//                     order_data.leg3 = BoxBiddingLegs::OTM_CALL;
//                     order_data.leg4 = BoxBiddingLegs::ITM_CALL;
//                     break;
//                 }
//                 default:
//                 {
//                     LOG_COUT("wrong leg came");
//                     break;
//                 }
//                 }

//                 short leg_count = 0;
//                 for (auto itr : s.legs)
//                 {
//                     std::cout << "leg " << leg_count++ << '\n';
//                     std::cout << "Lot size: " << itr.lot_size << " strike price: " << itr.strike_price << " symbol token: " << itr.symbol_token << '\n';
//                 }

//                 break;
//             }
//             case StrategyKind::BOX_2_1_1:
//             {
//                 auto &box_params = s.params.box_ioc;
//                 auto &box_order_data = s.order_data.box_ioc;
//                 box_params.portfolio_id = pf_id;
//                 box_order_data.portfolio_id = pf_id;
//                 box_params.max_lots = msg.add.params.BOX_2_1_1.max_lots;
//                 box_params.sol = msg.add.params.BOX_2_1_1.sol;
//                 box_params.is_flip_box = msg.add.params.BOX_2_1_1.flipBoxEnabled;
//                 box_order_data.is_flip_box = msg.add.params.BOX_2_1_1.flipBoxEnabled;

//                 box_params.price_difference = msg.add.params.BOX_2_1_1.priceDifference;
//                 box_params.timer_ms = msg.add.params.BOX_2_1_1.coverLegsTimeoutUs;

//                 uint32_t min_strike = std::min({s.legs[0].strike_price, s.legs[1].strike_price, s.legs[2].strike_price, s.legs[3].strike_price});
//                 uint32_t max_strike = std::max({s.legs[0].strike_price, s.legs[1].strike_price, s.legs[2].strike_price, s.legs[3].strike_price});

//                 // Now assign tokens based on box type
//                 for (int i = 0; i < 4; i++)
//                 {
//                     if (s.legs[i].leg_type == LegType::Call)
//                     {
//                         if (s.legs[i].strike_price == min_strike)
//                         {
//                             box_params.call_itm_token = s.legs[i].symbol_token;
//                         }
//                         else
//                         {
//                             box_params.call_otm_token = s.legs[i].symbol_token;
//                         }
//                     }
//                     else
//                     {
//                         if (s.legs[i].strike_price == min_strike)
//                         {
//                             box_params.put_otm_token = s.legs[i].symbol_token;
//                         }
//                         else
//                         {
//                             box_params.put_itm_token = s.legs[i].symbol_token;
//                         }
//                     }
//                 }

//                 box_order_data.strike_difference = max_strike - min_strike;

//                 if (!already_exists)
//                 {
//                     box_order_data.traded_qty = 0;
//                 }

//                 box_order_data.state = BoxIocStrategyState::IDLE;
//                 LOG_FILE(module, "traded qty:" + std::to_string(s.order_data.three_leg_bidding.traded_qty));

//                 LOG_FILE(module, "BoxIocStrategyParams:");
//                 LOG_FILE(module, "  call_itm_token: " + std::to_string(box_params.call_itm_token));
//                 LOG_FILE(module, "  put_otm_token: " + std::to_string(box_params.put_otm_token));
//                 LOG_FILE(module, "  call_otm_token: " + std::to_string(box_params.call_otm_token));
//                 LOG_FILE(module, "  put_itm_token: " + std::to_string(box_params.put_itm_token));
//                 LOG_FILE(module, "  price_difference: " + std::to_string(box_params.price_difference));
//                 LOG_FILE(module, "  is_flip_box: " + std::string(box_params.is_flip_box ? "true" : "false"));
//                 LOG_FILE(module, "  max_lots: " + std::to_string(box_params.max_lots));
//                 LOG_FILE(module, "  sol: " + std::to_string(box_params.sol));
//                 LOG_FILE(module, "  timer_ms: " + std::to_string(box_params.timer_ms));
//                 LOG_FILE(module, "  strike diff: " + std::to_string(box_order_data.strike_difference));
//                 break;
//             }

//             default:
//                 LOG_COUT("Unknown strategy kind in Add message");
//                 LOG_LIVE(module, "Unknown strategy kind in Add message");
//                 break;
//             }

//             // Mark as initialized
//             if (!already_exists)
//             {
//                 portfolio_initialized[pf_id] = true;
//             }
//             else
//             {
//                 // LOG_COUT("Updated existing portfolio id: " << pf_id);
//             }

//             // Send ACK to frontend
//             FrontendMessage ack_msg{};
//             ack_msg.msg_type = FrontendMessageType::RunAck;
//             ack_msg.portfolio_id = pf_id;
//             LOG_FILE(module, "Sending to frontend13");
//             if (socketManager.getFrontendClientSocket() != -1)
//             {
//                 LOG_FILE(module, "Sending to frontend13 run ack with size of bytes sending: " + std::to_string(sizeof(ack_msg)));
//                 send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//             }

//             LOG_FILE(module, "Run handled for pf id: " + std::to_string(pf_id));
//             LOG_FILE(module, "Run handled isactive: " + std::to_string(s.is_active));
//             LOG_FILE(module, "Run handled isactive: " + std::to_string(portfolios[pf_id].is_active));

//             push_portfolio(portfolios[pf_id]); // pushing params to shm for db
//             push_params(s);                    // pushing params to shm for db
//             push_order_data(s);                // pushing order data to shm for db
//             break;
//         }

//         case FrontendMessageType::Edit:
//         {

//             LOG_FILE(module, "EDIT CAME FOR pf id:" + std::to_string(msg.portfolio_id));
//             auto &it = portfolios[msg.portfolio_id];

//             switch (it.kind)
//             {
//             case StrategyKind::CONREV_IOC:
//             {

//                 auto &conrev_params = it.params.conrev;
//                 conrev_params.max_lots = msg.edit.params.CONREV_IOC.max_lots;
//                 LOG_FILE(module, "Max lot for pf id :" + std::to_string(msg.portfolio_id) + " max lots:" + std::to_string(conrev_params.max_lots));
//                 conrev_params.sol = msg.edit.params.CONREV_IOC.sol;
//                 conrev_params.con_flag = msg.edit.params.CONREV_IOC.con_flag;
//                 LOG_FILE(module, "con flag: " + std::to_string(msg.edit.params.CONREV_IOC.con_flag));

//                 conrev_params.spread = msg.edit.params.CONREV_IOC.spread;

//                 LOG_FILE(module, "Edited Traded:" + std::to_string(it.order_data.conrev.traded_qty));
//                 LOG_FILE(module, "Edited Order:" + std::to_string(it.order_data.conrev.ordered_qty));
//                 break;
//             }
//             case StrategyKind::CONREV_BID:
//             {

//                 auto &bidding_params = it.params.three_leg_bidding;
//                 bidding_params.max_lots = msg.edit.params.CONREV_BID.max_lots;
//                 bidding_params.sol = msg.edit.params.CONREV_BID.sol;
//                 bidding_params.spread = msg.edit.params.CONREV_BID.spread;

//                 bidding_params.is_opportunity = msg.edit.params.CONREV_BID.opp_check;
//                 bidding_params.leg1_spread_threshold = msg.edit.params.CONREV_BID.diff;
//                 bidding_params.legs2_timeout_us = msg.edit.params.CONREV_BID.timer;
//                 bidding_params.legs3_timeout_us = msg.edit.params.CONREV_BID.timer;

//                 break;
//             }
//             case StrategyKind::BOX_1_1_1_1:
//             {
//                 auto &bidding_params = it.params.box_bidding;
//                 bidding_params.max_lots = msg.edit.params.BOX_1_1_1_1.maxLots;
//                 bidding_params.sol = msg.edit.params.BOX_1_1_1_1.sol;
//                 bidding_params.flip_box_enabled = msg.edit.params.BOX_1_1_1_1.flipBoxEnabled;
//                 bidding_params.price_difference = msg.edit.params.BOX_1_1_1_1.priceDifference;
//                 if (bidding_params.flip_box_enabled)
//                     bidding_params.flip_price_difference = msg.edit.params.BOX_1_1_1_1.priceDifference;

//                 bidding_params.is_opportunity = msg.edit.params.BOX_1_1_1_1.isOpportunity;
//                 bidding_params.leg1_spread_threshold = msg.edit.params.BOX_1_1_1_1.leg1SpreadThreshold;
//                 bidding_params.leg1_timeout_us = msg.edit.params.BOX_1_1_1_1.TimeoutUs;
//                 bidding_params.legs2_timeout_us = msg.edit.params.BOX_1_1_1_1.TimeoutUs;
//                 bidding_params.legs3_timeout_us = msg.edit.params.BOX_1_1_1_1.TimeoutUs;
//                 bidding_params.legs4_timeout_us = msg.edit.params.BOX_1_1_1_1.TimeoutUs;

//                 LOG_FILE(module, "leg1_timeout_us: " + std::to_string(bidding_params.leg1_timeout_us) + " " + std::to_string(bidding_params.leg1_timeout_us) + " " + std::to_string(bidding_params.leg1_timeout_us) + " " + std::to_string(bidding_params.leg1_timeout_us));

//                 break;
//             }
//             case StrategyKind::BOX_2_1_1:
//             {
//                 auto &box_params = it.params.box_ioc;
//                 auto &box_order_data = it.order_data.box_ioc;
//                 box_params.max_lots = msg.edit.params.BOX_2_1_1.max_lots;
//                 box_params.sol = msg.edit.params.BOX_2_1_1.sol;
//                 box_params.is_flip_box = msg.edit.params.BOX_2_1_1.flipBoxEnabled;
//                 box_order_data.is_flip_box = msg.edit.params.BOX_2_1_1.flipBoxEnabled;

//                 box_params.price_difference = msg.edit.params.BOX_2_1_1.priceDifference;
//                 box_params.timer_ms = msg.edit.params.BOX_2_1_1.coverLegsTimeoutUs;

//                 LOG_FILE(module, "BoxIocStrategyParams:");
//                 LOG_FILE(module, "  call_itm_token: " + std::to_string(box_params.call_itm_token));
//                 LOG_FILE(module, "  put_otm_token: " + std::to_string(box_params.put_otm_token));
//                 LOG_FILE(module, "  call_otm_token: " + std::to_string(box_params.call_otm_token));
//                 LOG_FILE(module, "  put_itm_token: " + std::to_string(box_params.put_itm_token));
//                 LOG_FILE(module, "  price_difference: " + std::to_string(box_params.price_difference));
//                 LOG_FILE(module, "  is_flip_box: " + std::string(box_params.is_flip_box ? "true" : "false"));
//                 LOG_FILE(module, "  max_lots: " + std::to_string(box_params.max_lots));
//                 LOG_FILE(module, "  sol: " + std::to_string(box_params.sol));
//                 LOG_FILE(module, "  timer_ms: " + std::to_string(box_params.timer_ms));
//                 LOG_FILE(module, "  strike diff: " + std::to_string(box_order_data.strike_difference));
//                 break;
//             }

//             default:
//                 LOG_COUT("Unknown strategy kind in Edit message");
//                 std::cerr << "Unknown strategy kind in Edit message\n";
//                 break;
//             }

//             LOG_COUT("Edited portfolio:" << msg.portfolio_id);
//             LOG_FILE(module, "Edited portfolio:" + std::to_string(msg.portfolio_id));
//             LOG_FILE(module, "Active:" + std::to_string(it.is_active));
//             LOG_FILE(module, "Terminate:" + std::to_string(it.terminate));

//             FrontendMessage ack_msg{};
//             ack_msg.msg_type = FrontendMessageType::EditAck;
//             ack_msg.portfolio_id = msg.portfolio_id;
//             LOG_FILE(module, "Sending to frontend16");
//             if (socketManager.getFrontendClientSocket() != -1)
//             {
//                 send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//             }
//             push_portfolio(it);
//             push_params(it);
//             push_order_data(it);
//             break;
//         }

//         case FrontendMessageType::Stop:
//         {
//             // we can not deactivate directly if its in running state than.
//             LOG_FILE(module, "Stop came for pf id: " + std::to_string(msg.portfolio_id));
//             LOG_LIVE(module, "Stop came for pf id: " + std::to_string(msg.portfolio_id));
//             auto &portfolio = portfolios[msg.portfolio_id];
//             portfolio.stop_requested = true; // Mark for stopping

//             FrontendMessage ack_msg{};
//             ack_msg.msg_type = FrontendMessageType::StopAck;
//             ack_msg.portfolio_id = msg.portfolio_id;
//             ack_msg.ack.req_status = RequestStatus::NotStopped;
//             ack_msg.ack.status_reason = StatusReason::IterationOn;
//             LOG_FILE(module, "iteration1:?" + std::to_string(portfolio.is_iter_over));

//             if (portfolio.is_iter_over)
//             {
//                 LOG_FILE(module, "iteration2:?" + std::to_string(portfolio.is_iter_over));
//                 portfolio.is_active = false;
//                 ack_msg.ack.req_status = RequestStatus::Stopped;
//             }
//             else
//             {
//                 portfolio.stop_reason = UpdateReason::DelayedStopped;
//             }
//             LOG_FILE(module, "Sending to frontend14");
//             if (socketManager.getFrontendClientSocket() != -1)
//             {
//                 send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//             }
//             push_portfolio(portfolios[msg.portfolio_id]);

//             break;
//         }

//         case FrontendMessageType::ForceStop:
//         {
//             // we can not deactivate directly if its in running state than.
//             LOG_FILE(module, "ForceStop came");
//             auto sids = msg.forcestop.sids;
//             auto pf_count = msg.forcestop.sid_count;
//             LOG_FILE(module, "ForceStop came with count : " + std::to_string(msg.forcestop.sid_count));

//             for (int i = 0; i < pf_count; i++)
//             {
//                 uint16_t portfolio_id = msg.forcestop.sids[i];
//                 LOG_FILE(module, "ForceStop came for pf id:" + std::to_string(portfolio_id));

//                 if (portfolio_id < MAX_PORTFOLIOS && portfolio_initialized[portfolio_id])
//                 {
//                     auto &portfolio = portfolios[portfolio_id];
//                     if (!portfolio.is_active)
//                     {
//                         LOG_FILE(module, "ForceStop came for pf id but its not active:" + std::to_string(portfolio_id));

//                         continue;
//                     }
//                     portfolio.is_active = false;
//                     if (!portfolio.is_iter_over)
//                     {
//                         portfolio.stale_status = StaleStatus::FORCE_STOP;
//                         FrontendMessage status_update{};
//                         status_update.msg_type = FrontendMessageType::StaleStatusUpdate; // FORCESTOPSTALE, RMSStale
//                         status_update.portfolio_id = portfolio_id;
//                         status_update.stale_status_update.status = StaleStatus::FORCE_STOP;
//                         if (socketManager.getFrontendClientSocket() != -1)
//                         {
//                             LOG_FILE(module, "Sending Force Stop stale status");
//                             send(socketManager.getFrontendClientSocket(), &status_update, sizeof(status_update), MSG_DONTWAIT);
//                         }
//                     }
//                     else
//                     {

//                         FrontendMessage ack_msg{};
//                         ack_msg.msg_type = FrontendMessageType::ForceStopAck;
//                         ack_msg.portfolio_id = portfolio_id;
//                         if (socketManager.getFrontendClientSocket() != -1)
//                         {
//                             LOG_FILE(module, "ForceStop Ack sending to frontend");

//                             send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//                         }
//                     }

//                     // Cancel all open orders for this portfolio
//                     auto it = open_orders.find(portfolio_id);
//                     if (it != open_orders.end())
//                     {

//                         for (uint32_t oms_order_id : it->second)
//                         {
//                             LOG_FILE(module, "ForceStop came for pf id :" + std::to_string(portfolio_id) + ", Active Orders are sending cancel for:" + std::to_string(oms_order_id));
//                             LOG_LIVE(module, "ForceStop came for pf id :" + std::to_string(portfolio_id) + ", Active Orders are sending cancel for:" + std::to_string(oms_order_id));
//                             bool success = order_manager->sendCancelPlacement(portfolio_id, oms_order_id);
//                             if (!success)
//                             {
//                                 LOG_FILE(module, "Failed to cancel order " + std::to_string(oms_order_id) +
//                                                      " for pf id: " + std::to_string(portfolio_id));
//                             }
//                         }

//                         // Optionally clear the orders after cancellation
//                         it->second.clear();
//                     }

//                     push_portfolio(portfolio); // pushing portoflio to shm for db
//                 }
//             }

//             break;
//         }

//         case FrontendMessageType::Remove:
//         {

//             LOG_FILE(module, "Remove came with pf id:" + std::to_string(msg.portfolio_id));

//             auto &it = portfolios[msg.portfolio_id];
//             if (it.portfolio_id == -1)
//             {
//                 FrontendMessage ack_msg{};
//                 ack_msg.msg_type = FrontendMessageType::RemoveAck;
//                 ack_msg.portfolio_id = msg.portfolio_id;
//                 ack_msg.ack.req_status = RequestStatus::NotRemoved;
//                 LOG_FILE(module, "Sending to frontend11");
//                 if (socketManager.getFrontendClientSocket() != -1)
//                 {
//                     send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//                 }
//                 LOG_FILE(module, "not remove pf id not exist pf id is: " + std::to_string(msg.portfolio_id));

//                 break;
//             }
//             portfolio_initialized[msg.portfolio_id] = false;
//             it.is_active = 0;
//             it.terminate = 1;

//             FrontendMessage ack_msg{};
//             ack_msg.msg_type = FrontendMessageType::RemoveAck;
//             ack_msg.portfolio_id = msg.portfolio_id;
//             ack_msg.ack.req_status = RequestStatus::Removed;
//             LOG_FILE(module, "Sending to frontend12");
//             if (socketManager.getFrontendClientSocket() != -1)
//             {
//                 send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//             }

//             break;
//         }

//         case FrontendMessageType::RecoveryRequest:
//         {

//             std::map<uint16_t, PortfolioShm> portfolio_map;

//             for (int i = 0; i < MAX_PORTFOLIOS; ++i)
//             {
//                 if (portfolio_initialized[i] == false)
//                     continue;
//                 const Portfolio &src = portfolios[i];

//                 PortfolioShm dst;
//                 dst.portfolio_id = src.portfolio_id;
//                 dst.kind = src.kind;
//                 dst.leg_count = src.leg_count;
//                 dst.is_active = src.is_active;
//                 dst.terminate = src.terminate;
//                 dst.is_iter_over = src.is_iter_over;
//                 dst.stale_status = src.stale_status;
//                 dst.stop_requested = src.stop_requested;
//                 dst.traded_qty = src.traded_qty;
//                 dst.achieved_spread = src.achieved_spread;

//                 for (int j = 0; j < 4; ++j)
//                 {
//                     dst.legs[j] = src.legs[j];
//                 }

//                 dst.params.kind = src.kind;

//                 switch (src.kind)
//                 {
//                 case StrategyKind::CONREV_IOC:
//                 {
//                     dst.params.CONREV_IOC.con_flag = src.params.conrev.con_flag;
//                     dst.params.CONREV_IOC.max_lots = src.params.conrev.max_lots;
//                     dst.params.CONREV_IOC.sol = src.params.conrev.sol;
//                     dst.params.CONREV_IOC.spread = src.params.conrev.spread;

//                     break;
//                 }
//                 case StrategyKind::CONREV_BID:
//                 {
//                     dst.params.CONREV_BID.con_flag = src.params.three_leg_bidding.con_flag;
//                     dst.params.CONREV_BID.max_lots = src.params.three_leg_bidding.max_lots;
//                     dst.params.CONREV_BID.sol = src.params.three_leg_bidding.sol;
//                     dst.params.CONREV_BID.spread = src.params.three_leg_bidding.spread;

//                     dst.params.CONREV_BID.diff = src.params.three_leg_bidding.leg1_spread_threshold;
//                     dst.params.CONREV_BID.opp_check = src.params.three_leg_bidding.is_opportunity;
//                     dst.params.CONREV_BID.timer = src.params.three_leg_bidding.legs2_timeout_us;

//                     break;
//                 }
//                 case StrategyKind::BOX_1_1_1_1:
//                 {
//                     dst.params.BOX_1_1_1_1.maxLots = src.params.box_bidding.max_lots;
//                     dst.params.BOX_1_1_1_1.sol = src.params.box_bidding.sol;
//                     dst.params.BOX_1_1_1_1.flipBoxEnabled = src.params.box_bidding.flip_box_enabled;
//                     dst.params.BOX_1_1_1_1.priceDifference = src.params.box_bidding.price_difference;
//                     if (dst.params.BOX_1_1_1_1.flipBoxEnabled)
//                         dst.params.BOX_1_1_1_1.priceDifference = src.params.box_bidding.flip_price_difference;

//                     dst.params.BOX_1_1_1_1.isOpportunity = src.params.box_bidding.is_opportunity;
//                     dst.params.BOX_1_1_1_1.TimeoutUs = src.params.box_bidding.leg1_timeout_us;
//                     dst.params.BOX_1_1_1_1.leg1SpreadThreshold = src.params.box_bidding.leg1_spread_threshold;
//                     dst.params.BOX_1_1_1_1.entry_leg = src.params.box_bidding.entry_leg;

//                     std::cout << "max_lots: " << dst.params.BOX_1_1_1_1.maxLots << std::endl;
//                     std::cout << "sol: " << dst.params.BOX_1_1_1_1.sol << std::endl;
//                     std::cout << "flipBoxEnabled: " << dst.params.BOX_1_1_1_1.flipBoxEnabled << std::endl;
//                     std::cout << "priceDifference: " << dst.params.BOX_1_1_1_1.priceDifference << "src.priceDifference: " << src.params.box_bidding.price_difference << std::endl;
//                     std::cout << "isOpportunity: " << dst.params.BOX_1_1_1_1.isOpportunity << std::endl;
//                     std::cout << "TimeoutUs: " << dst.params.BOX_1_1_1_1.TimeoutUs << " src.TimeoutUs " << src.params.box_bidding.leg1_timeout_us << std::endl;
//                     std::cout << "leg1SpreadThreshold: " << dst.params.BOX_1_1_1_1.leg1SpreadThreshold << std::endl;
//                     std::cout << "entry_leg: " << dst.params.BOX_1_1_1_1.entry_leg << std::endl;

//                     break;
//                 }
//                 case StrategyKind::BOX_2_1_1:
//                 {

//                     const auto &b = src.params.box_ioc; // adjust to your actual source path

//                     dst.params.BOX_2_1_1.max_lots = b.max_lots;
//                     dst.params.BOX_2_1_1.sol = b.sol;
//                     dst.params.BOX_2_1_1.flipBoxEnabled = b.is_flip_box;       // true => reversion, false => conversion
//                     dst.params.BOX_2_1_1.priceDifference = b.price_difference; // paise (int64_t)

//                     dst.params.BOX_2_1_1.coverLegsTimeoutUs = (b.timer_ms);

//                     break;
//                 }

//                 default:
//                     LOG_COUT("Unknown strategy kind in Recovery Request message");
//                     break;
//                 }

//                 portfolio_map[dst.portfolio_id] = dst;
//             }

//             std::cout << "---- Portfolio Map ----\n";
//             for (const auto &[id, p] : portfolio_map)
//             {
//                 std::cout << "Portfolio ID: " << p.portfolio_id << "\n";
//                 std::cout << "  Kind: " << static_cast<int>(p.kind) << "\n";
//                 std::cout << "  Leg Count: " << static_cast<int>(p.leg_count) << "\n";
//                 std::cout << "  is_active: " << p.is_active << "\n";
//                 std::cout << "  terminate: " << p.terminate << "\n";
//                 std::cout << "  is_iter_over: " << p.is_iter_over << "\n";
//                 std::cout << "  stale_status: " << static_cast<int>(p.stale_status) << "\n";
//                 std::cout << "  stop_requested: " << p.stop_requested << "\n";
//                 std::cout << "  traded_qty: " << p.traded_qty << "\n";
//                 std::cout << "  achieved_spread: " << p.achieved_spread << "\n";

//                 for (int j = 0; j < 4; ++j)
//                 {
//                     const auto &leg = p.legs[j];
//                     std::cout << "  Leg[" << j << "]: token=" << leg.symbol_token
//                               << ", strike=" << leg.strike_price
//                               << ", side=" << static_cast<int>(leg.side)
//                               << ", lotsize=" << leg.lot_size
//                               << ", pro=" << leg.is_pro_account
//                               << ", type=" << static_cast<int>(leg.leg_type) << "\n";
//                 }
//                 std::cout << "------------------------\n";
//             }

//             std::cout << "---- OMS to Leg Map ----\n";
//             for (const auto &[oms_id, leg] : oms_to_leg)
//             {
//                 std::cout << "OMS ID: " << oms_id << "\n";
//                 std::cout << "  Token: " << leg.token << "\n";
//                 std::cout << "  Side: " << static_cast<int>(leg.side) << "\n";
//                 std::cout << "  Portfolio ID: " << leg.portfolio_id << "\n";
//                 std::cout << "  Fill Price Sum: " << leg.fill_price_sum << "\n";
//                 std::cout << "  Fill Qty Sum: " << leg.fill_qty_sum << "\n";
//                 std::cout << "  Required Qty: " << leg.required_qty << "\n";
//                 std::cout << "  OMS Order ID: " << leg.oms_order_id << "\n";
//                 std::cout << "  Exchange Order ID: " << leg.exchange_order_id << "\n";
//                 std::cout << "  Exchange Modified Time: " << leg.exchange_modified_time << "\n";
//                 std::cout << "  Order State: " << static_cast<int>(leg.order_state) << "\n";
//                 std::cout << "------------------------\n";
//             }

//             std::vector<Trade> trades;

//             recover_trades(config.redis_host, config.redis_port, trades, config.redis_db_index);

//             std::cout << "---- Recovered Trades Hello ----\n";
//             for (const auto &t : trades)
//             {
//                 std::cout << "Trade: oms_order_id=" << t.oms_order_id
//                           << ", fill_qty=" << t.fill_qty
//                           << ", fill_price=" << t.fill_price
//                           << ", exchange_order_id=" << t.exchange_order_id
//                           << ", timestamp=" << t.timestamp
//                           << ", partial_fill=" << (t.partial_fill ? "true" : "false") << "\n";
//             }
//             std::cout << "------------------------\n";

//             std::ostringstream oss;

//             // Serialize portfolio_map
//             uint32_t portfolio_count = portfolio_map.size();
//             oss.write(reinterpret_cast<const char *>(&portfolio_count), sizeof(portfolio_count));
//             for (const auto &[id, p] : portfolio_map)
//             {
//                 oss.write(reinterpret_cast<const char *>(&p), sizeof(PortfolioShm));
//             }

//             // Serialize oms_to_leg
//             uint32_t oms_count = oms_to_leg.size();
//             oss.write(reinterpret_cast<const char *>(&oms_count), sizeof(oms_count));
//             for (const auto &[oms_id, leg] : oms_to_leg)
//             {
//                 oss.write(reinterpret_cast<const char *>(&oms_id), sizeof(oms_id));
//                 oss.write(reinterpret_cast<const char *>(&leg), sizeof(StrategyLegData));
//             }

//             // Serialize trades
//             uint32_t trade_count = trades.size();
//             oss.write(reinterpret_cast<const char *>(&trade_count), sizeof(trade_count));
//             for (const auto &t : trades)
//             {
//                 oss.write(reinterpret_cast<const char *>(&t), sizeof(Trade));
//             }

//             std::string payload = oss.str();

//             LOG_FILE(module, "Sending to frontend recovery with bytes:" + std::to_string(payload.size()) +
//                                  " (Portfolios:" + std::to_string(portfolio_count) +
//                                  ", OMS:" + std::to_string(oms_count) +
//                                  ", Trades:" + std::to_string(trade_count) + ")");

//             FrontendMessage ack_msg{};
//             ack_msg.msg_type = FrontendMessageType::RecoveryRequestAck;
//             ack_msg.recovery_ack.recovery_bytes = payload.size();
//             LOG_FILE(module, "Sending to frontend RecoveryRequestAck with bytes:" + std::to_string(payload.size()));
//             if (socketManager.getFrontendClientSocket() != -1)
//             {
//                 send(socketManager.getFrontendClientSocket(), &ack_msg, sizeof(ack_msg), MSG_DONTWAIT);
//             }

//             if (socketManager.getFrontendClientSocket() != -1)
//             {
//                 send(socketManager.getFrontendClientSocket(), payload.data(), payload.size(), MSG_DONTWAIT);
//             }

//             break;
//         }
//         }
//     }

//     // Log buffer utilization if it's getting high
//     if (frontend_rx_buffer.utilization() > 80.0f) [[unlikely]]
//     {
//         LOG_FILE(module, "WARNING: Frontend buffer utilization at " +
//                              std::to_string(frontend_rx_buffer.utilization()) + "%");
//     }
// }

std::string HFTStrategyEngine::get_nse_fo_contract_name()
{
    // Get current date
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm = *std::localtime(&t);

    // Format date as DDMMYYYY
    std::ostringstream oss;
    oss << "../config/NSE_FO_contract_"
        << std::setw(2) << std::setfill('0') << local_tm.tm_mday
        << std::setw(2) << std::setfill('0') << (local_tm.tm_mon + 1)
        << (local_tm.tm_year + 1900)
        << ".csv";

    return oss.str();
}



/* ════════════════════════════════════════════════════════════
 * PLATFORM API IMPLEMENTATIONS
 * ════════════════════════════════════════════════════════════ */


int32_t HFTStrategyEngine::api_new_order_multi_leg(PlatformContext* ctx, uint32_t pf_id,
                                       uint32_t token, uint8_t side,
                                       const OrderLeg* legs, uint8_t leg_count, uint8_t order_type)
{   

    // want to use this function sendOrderPlacement of order manager
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    // Validate portfolio slot
    if (pf_id >= MAX_PORTFOLIOS || !engine->portfolio_slots[pf_id].allocated) {
        return -1; 
    }
    // we support 3/2/1 leg ioc, single leg bidding only 
    if (leg_count == 0 || leg_count > 3) {
        return -4; // Invalid leg count
    }
    // Generate OMS ID for the multi-leg order (can be used as a group ID)

    for(int i = 0; i < leg_count; i++) {
        if (legs[i].qty <= 0 || legs[i].price <= 0) {
            return -5; // Invalid leg parameters
        }
        legs[i].oms_order_id = StrategyOrderIDManager::instance().generate();
    }

    bool sent = engine->order_manager->sendOrderPlacement(pf_id, (OrderType)order_type, legs, leg_count, __rdtsc(), true);
    if (!sent) {
        LOG_FILE("PLATFORM_API", "Failed to send multi-leg order: pf=" + std::to_string(pf_id));
        return -2;
    }

    for(int i = 0; i < leg_count; i++) {
        const auto& leg = legs[i];
        engine->oms_to_leg[leg.oms_order_id] = StrategyLegData{
            .token = leg.symbol_id,
            .side = (OrderSide)leg.side,
            .portfolio_id = static_cast<uint16_t>(pf_id),
            .fill_price_sum = 0,
            .fill_qty_sum = 0,
            .ordered_price = static_cast<uint32_t>(leg.price),
            .required_qty = static_cast<uint32_t>(leg.qty),
            .oms_order_id = leg.oms_order_id,
            .exchange_order_id = 0,
            .exchange_modified_time = 0,
            .order_state = OrderState::NewOms
        };

        LOG_FILE("PLATFORM_API", "Multi-leg order placed: pf=" + std::to_string(pf_id) + 
                 " oms=" + std::to_string(leg.oms_order_id) + " token=" + std::to_string(leg.symbol_id) +
                 " side=" + std::to_string(leg.side) + " price=" + std::to_string(leg.price) +
                 " qty=" + std::to_string(leg.qty));
    }

    return 0;
}


int32_t HFTStrategyEngine::api_place_modify_order(
    PlatformContext* ctx, uint32_t pf_id,
    uint32_t oms_order_id,
    int64_t new_price, int32_t new_qty)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    
    if (pf_id >= MAX_PORTFOLIOS || !engine->portfolio_slots[pf_id].allocated) {
        return -1;
    }
    
    // Find order
    auto it = engine->oms_to_leg.find(oms_order_id);
    if (it == engine->oms_to_leg.end()) {
        return -3;  // Not found
    }
    
    // Check if no-op (dedup)
    if (it->second.ordered_price == new_price && it->second.required_qty == new_qty) {
        return -2;  // No-op
    }
    
    // Create modify leg
    Leg leg;
    leg.symbol_id = it->second.token;
    leg.side = it->second.side;
    leg.price = new_price;
    leg.qty = new_qty;
    leg.oms_order_id = oms_order_id;
    
    bool sent = engine->order_manager->sendModifyPlacement(
        pf_id, oms_order_id, leg, __rdtsc(), 0);
    
    if (!sent) {
        LOG_FILE("PLATFORM_API", "Failed to modify order: oms=" + std::to_string(oms_order_id));
        return -2;
    }
    
    LOG_FILE("PLATFORM_API", "Order modified: oms=" + std::to_string(oms_order_id) +
             " price=" + std::to_string(new_price) + " qty=" + std::to_string(new_qty));
    
    return 0;
}

int32_t HFTStrategyEngine::api_place_cancel_order(
    PlatformContext* ctx, uint32_t pf_id, uint32_t oms_order_id)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    
    if (pf_id >= MAX_PORTFOLIOS || !engine->portfolio_slots[pf_id].allocated) {
        return -1;
    }
    
    bool sent = engine->order_manager->sendCancelPlacement(pf_id, oms_order_id);
    
    if (!sent) {
        LOG_FILE("PLATFORM_API", "Failed to cancel order: oms=" + std::to_string(oms_order_id));
        return -2;
    }
    
    LOG_FILE("PLATFORM_API", "Order cancelled: oms=" + std::to_string(oms_order_id));
    
    return 0;
}

int32_t HFTStrategyEngine::api_get_position(
    PlatformContext* ctx, uint32_t pf_id,
    uint32_t token, PositionView* out)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    
    // You need to track positions - for now return not found
    // TODO: Implement position tracking similar to your existing logic
    return -1;
}

int32_t HFTStrategyEngine::api_get_open_orders(
    PlatformContext* ctx, uint32_t pf_id,
    OpenOrderView* out_buf, int32_t max)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    
    auto it = engine->open_orders.find(pf_id);
    if (it == engine->open_orders.end()) {
        return 0;
    }
    
    int32_t count = 0;
    for (uint32_t oms_id : it->second) {
        if (count >= max) break;
        
        auto leg_it = engine->oms_to_leg.find(oms_id);
        if (leg_it == engine->oms_to_leg.end()) continue;
        
        out_buf[count].oms_order_id = oms_id;
        out_buf[count].token = leg_it->second.token;
        out_buf[count].side = (uint8_t)leg_it->second.side;
        out_buf[count].price = leg_it->second.ordered_price;
        out_buf[count].qty = leg_it->second.required_qty;
        out_buf[count].state = leg_it->second.order_state;
        
        count++;
    }
    
    return count;
}

void HFTStrategyEngine::api_log_msg(
    PlatformContext* ctx, uint32_t pf_id,
    const char* msg, uint32_t len)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    
    std::string log_str(msg, len);
    LOG_FILE("STRATEGY_" + std::to_string(pf_id), log_str);
}

void HFTStrategyEngine::api_send_status_update(
    PlatformContext* ctx,
    uint32_t pf_id,
    const StrategyStatusUpdate* update)
{
    HFTStrategyEngine* engine = (HFTStrategyEngine*)ctx;
    
    // Build frontend message
    FrontendResponse resp;
    resp.pf_id = pf_id;
    resp.status = 0;
    
    // Format as JSON
    char json[4096];
    int len = snprintf(json, sizeof(json),
        "{\"type\":\"status_update\","
        "\"pf_id\":%u,"
        "\"traded_qty\":%d,"
        "\"achieved_spread\":%d,"
        "\"current_spread\":%d,"           // NEW
        "\"has_opportunity\":%d,"          // NEW
        "\"is_complete\":%d",
        update->pf_id,
        update->traded_qty,
        update->achieved_spread,
        update->current_spread,            // NEW
        update->has_opportunity,           // NEW
        update->is_complete);
    
    // Add custom data if present
    if (update->custom_data_len > 0) {
        len += snprintf(json + len, sizeof(json) - len, ",\"custom\":\"");
        
        for (uint32_t i = 0; i < update->custom_data_len && i < 100; i++) {
            len += snprintf(json + len, sizeof(json) - len,
                "%02x", update->custom_data[i]);
        }
        
        len += snprintf(json + len, sizeof(json) - len, "\"");
    }
    
    snprintf(json + len, sizeof(json) - len, "}");
    
    memcpy(resp.response, json, strlen(json));
    resp.response_len = strlen(json);
    
    // Send to frontend
    if (engine->socketManager.getFrontendClientSocket() != -1)
    {
        send(engine->socketManager.getFrontendClientSocket(), &resp,
             sizeof(FrontendResponse), MSG_DONTWAIT);
    }
    
    LOG_FILE("PLATFORM_API", "Status: pf=" + std::to_string(pf_id) +
             " current_spread=" + std::to_string(update->current_spread) +
             " achieved_spread=" + std::to_string(update->achieved_spread) +
             " opportunity=" + std::to_string(update->has_opportunity));
}

/* ════════════════════════════════════════════════════════════
 * PLUGIN MANAGEMENT
 * ════════════════════════════════════════════════════════════ */

bool HFTStrategyEngine::load_strategy_plugin(const char* so_path)
{
    if (plugin_count >= 32) {
        LOG_FILE(module, "Maximum plugins loaded");
        LOG_LIVE(module, "Maximum plugins loaded");
        return false;
    }
    
    void* handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        LOG_FILE(module, "dlopen failed: " + std::string(dlerror()));
        LOG_LIVE(module, "dlopen failed: " + std::string(dlerror()));
        return false;
    }
    
    // Resolve symbols
    auto get_type_id = (uint32_t(*)(void))dlsym(handle, "strategy_get_type_id");
    auto create = (void*(*)(StrategyFnTable*))dlsym(handle, "strategy_create");
    auto destroy_all = (void(*)(void))dlsym(handle, "strategy_destroy_all");
    
    if (!get_type_id || !create || !destroy_all) {
        LOG_FILE(module, "Failed to resolve symbols: " + std::string(dlerror()));
        LOG_LIVE(module, "Failed to resolve symbols: " + std::string(dlerror()));
        dlclose(handle);
        return false;
    }
    
    uint32_t type_id = get_type_id();
    
    // Check for duplicate
    for (uint8_t i = 0; i < plugin_count; i++) {
        if (plugins[i].type_id == type_id) {
            LOG_FILE(module, "Plugin with type_id " + std::to_string(type_id) + " already loaded");
            LOG_LIVE(module, "Plugin with type_id " + std::to_string(type_id) + " already loaded");
            dlclose(handle);
            return false;
        }
    }
    
    // Store plugin
    StrategyPlugin& plugin = plugins[plugin_count++];
    plugin.dl_handle = handle;
    plugin.type_id = type_id;
    plugin.get_type_id = get_type_id;
    plugin.create = create;
    plugin.destroy_all = destroy_all;
    strncpy(plugin.so_path, so_path, sizeof(plugin.so_path) - 1);
    
    LOG_FILE(module, "Loaded plugin: " + std::string(so_path) + 
             " type_id=" + std::to_string(type_id));
    LOG_LIVE(module, "Loaded plugin: " + std::string(so_path) + 
             " type_id=" + std::to_string(type_id));
    
    return true;
}

void HFTStrategyEngine::unload_strategy_plugin(uint32_t type_id)
{
    for (uint8_t i = 0; i < plugin_count; i++) {
        if (plugins[i].type_id == type_id) {
            // Destroy all instances
            plugins[i].destroy_all();
            
            // Unload .so
            dlclose(plugins[i].dl_handle);
            
            // Remove from array
            for (uint8_t j = i; j < plugin_count - 1; j++) {
                plugins[j] = plugins[j + 1];
            }
            plugin_count--;
            
            LOG_FILE(module, "Unloaded plugin: type_id=" + std::to_string(type_id));
            return;
        }
    }
}

HFTStrategyEngine::StrategyPlugin* HFTStrategyEngine::find_plugin_by_type(uint32_t type_id)
{
    for (uint8_t i = 0; i < plugin_count; i++) {
        if (plugins[i].type_id == type_id) {
            return &plugins[i];
        }
    }
    return nullptr;
}
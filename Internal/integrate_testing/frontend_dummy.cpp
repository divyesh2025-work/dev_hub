#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../Utils/Types.h"
#include <map>
#include "../Library/flat_hash_map.hpp"
// void sendEditMessage(int sock, int portfolio_id)
// {
//     FrontendMessage edit_msg = {};
//     edit_msg.msg_type = FrontendMessageType::Edit;
//     edit_msg.portfolio_id = portfolio_id;

//     // Set strategy parameters for editing
//     edit_msg.edit.params = {
//         .max_lots = 750,
//         .sol = 750,
//         .forward_spread = 0,
//         .revers_spread = -1500,
//         .opp_check = 1,
//         .diff = -4000,
//         .timer = 500000
//     };

//     ssize_t sent = send(sock, &edit_msg, sizeof(edit_msg), 0);
//     if (sent < 0)
//     {
//         perror("Send EDIT failed");
//     }
//     else
//     {
//         std::cout << "EDIT message sent (" << sent << " bytes)\n";
//     }
// }

void sendAddMessgae(int sock)
{
    // Prepare ADD message
    FrontendMessage add_msg = {};
    add_msg.msg_type = FrontendMessageType::RecoveryRequest;
    // add_msg.portfolio_id = 5;
    // add_msg.add.strategy_kind = StrategyKind::CONREV_BID;
    // add_msg.add.leg_count = 3;
    // add_msg.add.is_active = false;
    // add_msg.add.terminate = false;

    // add_msg.add.params = {
    //     .max_lots = 750,
    //     .sol = 750,
    //     .forward_spread = 0,
    //     .revers_spread = -500,
    //     .opp_check = 1,
    //     .diff = -4000,
    //     .timer = 500000
    // };

    // add_msg.add.legs[0] = {
    //     .symbol_token = 53433,
    //     .strike_price = 81000,
    //     .option_type = OrderSide::Sell, // assuming Buy = 0
    //     .let_size = 50,
    //     .is_pro_account = true,
    //     .leg_type = LegType::Future
    // };

    // add_msg.add.legs[1] = {
    //     .symbol_token = 134082,
    //     .strike_price = 81000,
    //     .option_type = OrderSide::Buy, // assuming Buy = 0
    //     .let_size = 50,
    //     .is_pro_account = true,
    //     .leg_type = LegType::Call
    // };

    // add_msg.add.legs[2] = {
    //     .symbol_token = 134083,
    //     .strike_price = 81000,
    //     .option_type = OrderSide::Sell, // assuming Buy = 0
    //     .let_size = 50,
    //     .is_pro_account = true,
    //     .leg_type = LegType::Put
    // };

    ssize_t sent = send(sock, &add_msg, sizeof(add_msg), 0);
    if (sent < 0)
    {
        perror("Send ADD failed");
    }
    else
    {
        std::cout << "ADD message sent (" << sent << " bytes)\n";
    }
}
int main()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(5260);                       // Server port
    inet_pton(AF_INET, "10.180.2.16", &server_address.sin_addr); // Server IP

    if (connect(sock, (sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Connection failed");
        return 1;
    }

    sendAddMessgae(sock);
    sleep(1); // wait a bit before sending RUN
              // sendEditMessage(sock, 5);
    FrontendMessage msg;
    ssize_t received = recv(sock, &msg, sizeof(msg), MSG_DONTWAIT);

    if (msg.msg_type == FrontendMessageType::RecoveryRequestAck)
    {

        // // Prepare RUN message
        // FrontendMessage run_msg = {};
        // run_msg.msg_type = FrontendMessageType::Run;
        // run_msg.portfolio_id = 2;

        // sent = send(sock, &run_msg, sizeof(run_msg), 0);
        // if (sent < 0)
        // {
        //     perror("Send RUN failed");
        // }
        // else
        // {
        //     std::cout << "RUN message sent (" << sent << " bytes)\n";
        // }
        std::map<uint16_t, PortfolioShm> portfolio_map;
        ska::flat_hash_map<uint32_t, StrategyLegData> oms_to_leg;

        std::vector<char> buffer(8192); // Adjust size as needed
        ssize_t bytes_received = recv(sock, buffer.data(), buffer.size(), 0);
        if (bytes_received <= 0)
        {
            std::cerr << "Receive failed or connection closed.\n";
            return 0;
        }

        std::istringstream iss(std::string(buffer.data(), bytes_received));

        // Deserialize portfolio_map
        uint32_t portfolio_count;
        iss.read(reinterpret_cast<char *>(&portfolio_count), sizeof(portfolio_count));
        for (uint32_t i = 0; i < portfolio_count; ++i)
        {
            PortfolioShm p;
            iss.read(reinterpret_cast<char *>(&p), sizeof(PortfolioShm));
            portfolio_map[p.portfolio_id] = p;
        }

        // Deserialize oms_to_leg
        uint32_t oms_count;
        iss.read(reinterpret_cast<char *>(&oms_count), sizeof(oms_count));
        for (uint32_t i = 0; i < oms_count; ++i)
        {
            uint32_t oms_id;
            StrategyLegData leg;
            iss.read(reinterpret_cast<char *>(&oms_id), sizeof(oms_id));
            iss.read(reinterpret_cast<char *>(&leg), sizeof(StrategyLegData));
            oms_to_leg[oms_id] = leg;
        }

        std::cout << "---- Portfolio Map ----\n";
        for (const auto &[id, p] : portfolio_map)
        {
            std::cout << "Portfolio ID: " << p.portfolio_id << "\n";
            std::cout << "  Kind: " << static_cast<int>(p.kind) << "\n";
            std::cout << "  Leg Count: " << static_cast<int>(p.leg_count) << "\n";
            std::cout << "  is_active: " << p.is_active << "\n";
            std::cout << "  terminate: " << p.terminate << "\n";
            std::cout << "  is_iter_over: " << p.is_iter_over << "\n";
            std::cout << "  stop_requested: " << p.stop_requested << "\n";
            std::cout << "  traded_qty: " << p.traded_qty << "\n";
            std::cout << "  achieved_spread: " << p.achieved_spread << "\n";

            for (int i = 0; i < 4; ++i)
            {
                const auto &leg = p.legs[i];
                std::cout << "  Leg[" << i << "]: token=" << leg.symbol_token
                          << ", strike=" << leg.strike_price
                          << ", option=" << static_cast<int>(leg.option_type)
                          << ", lotsize=" << leg.let_size
                          << ", pro=" << leg.is_pro_account
                          << ", type=" << static_cast<int>(leg.leg_type) << "\n";
            }
            std::cout << "------------------------\n";
        }

        std::cout << "---- OMS to Leg Map ----\n";
        for (const auto &[oms_id, leg] : oms_to_leg)
        {
            std::cout << "OMS ID: " << oms_id << "\n";
            std::cout << "  Token: " << leg.token << "\n";
            std::cout << "  Side: " << static_cast<int>(leg.side) << "\n";
            std::cout << "  Portfolio ID: " << leg.portfolio_id << "\n";
            std::cout << "  Fill Price Sum: " << leg.fill_price_sum << "\n";
            std::cout << "  Fill Qty Sum: " << leg.fill_qty_sum << "\n";
            std::cout << "  Required Qty: " << leg.required_qty << "\n";
            std::cout << "  OMS Order ID: " << leg.oms_order_id << "\n";
            std::cout << "  Exchange Order ID: " << leg.exchange_order_id << "\n";
            std::cout << "  Exchange Modified Time: " << leg.exchange_modified_time << "\n";
            std::cout << "  Order State: " << static_cast<int>(leg.order_state) << "\n";
            std::cout << "------------------------\n";
        }
    }

    close(sock);
    return 0;
}

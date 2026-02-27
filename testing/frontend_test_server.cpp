/*
 * frontend_test_server.cpp - Interactive CLI for HFT Platform
 * 
 * Compile:
 *   g++ -O2 -std=c++20 -o frontend_test frontend_test_server.cpp
 * 
 * Usage:
 *   ./frontend_test localhost 8000
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

/* ═══════════════════════════════════════════════════════════
 * FRONTEND PROTOCOL (must match strategy_sdk.h)
 * ═══════════════════════════════════════════════════════════ */

typedef enum : uint8_t {
    FRONTEND_CMD_ADD = 1,
    FRONTEND_CMD_EDIT = 2,
    FRONTEND_CMD_RUN = 3,
    FRONTEND_CMD_STOP = 4,
    FRONTEND_CMD_REMOVE = 5,
    FRONTEND_CMD_QUERY = 6
} FrontendCmdType;

struct FrontendCommand {
    FrontendCmdType cmd_type;
    uint32_t        pf_id;
    uint32_t        payload_len;
    uint8_t         payload[4096];
} __attribute__((packed));

struct FrontendResponse {
    uint32_t        pf_id;
    int32_t         status;
    uint32_t        response_len;
    uint8_t         response[4096];
} __attribute__((packed));

/* ═══════════════════════════════════════════════════════════
 * STRATEGY PARAMETER STRUCTURES
 * ═══════════════════════════════════════════════════════════ */

// ConRev IOC Parameters
struct ConRevIOCParams {
    uint32_t fut_token;
    uint32_t call_token;
    uint32_t put_token;
    int32_t  strike_price;
    int32_t  max_lots;
    int32_t  sol;
    int32_t  spread_threshold;
    bool     is_conversion;
} __attribute__((packed));

// ConRev Bidding Parameters
struct ConRevBidParams {
    uint32_t fut_token;
    uint32_t call_token;
    uint32_t put_token;
    int32_t  strike_price;
    int32_t  max_lots;
    int32_t  spread_threshold;
    bool     is_conversion;
    bool     is_opportunity;
    int32_t  leg1_spread_threshold;
    uint64_t leg2_timeout_us;
    uint64_t leg3_timeout_us;
} __attribute__((packed));

// Box Bidding Parameters
struct BoxBiddingParams {
    uint32_t itm_call_token;
    uint32_t otm_put_token;
    uint32_t otm_call_token;
    uint32_t itm_put_token;
    int32_t  strike_diff;
    int32_t  max_lots;
    int32_t  price_difference;
    bool     flip_box_enabled;
    bool     is_opportunity;
    int32_t  leg1_spread_threshold;
    uint64_t leg1_timeout_us;
    uint64_t leg2_timeout_us;
    uint64_t leg3_timeout_us;
    uint64_t leg4_timeout_us;
    uint8_t  entry_leg;
} __attribute__((packed));

// Box IOC Parameters
struct BoxIOCParams {
    uint32_t call_itm_token;
    uint32_t put_otm_token;
    uint32_t call_otm_token;
    uint32_t put_itm_token;
    int32_t  strike_diff;
    int32_t  max_lots;
    int32_t  price_difference;
    bool     is_flip_box;
    uint64_t timer_ms;
} __attribute__((packed));

/* ═══════════════════════════════════════════════════════════
 * TYPE IDs (must match your strategy .so files)
 * ═══════════════════════════════════════════════════════════ */

// #define TYPE_CONREV_IOC     100
#define TYPE_CONREV_IOC     1
#define TYPE_CONREV_BID     101
#define TYPE_BOX_BIDDING    102
#define TYPE_BOX_IOC        103

/* ═══════════════════════════════════════════════════════════
 * TCP CONNECTION
 * ═══════════════════════════════════════════════════════════ */

int connect_to_platform(const char* host, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

/* ═══════════════════════════════════════════════════════════
 * SEND/RECEIVE HELPERS
 * ═══════════════════════════════════════════════════════════ */

bool send_command(int fd, const FrontendCommand& cmd)
{
    ssize_t sent = send(fd, &cmd, sizeof(cmd), 0);
    if (sent != sizeof(cmd)) {
        perror("send");
        return false;
    }
    return true;
}

bool recv_response(int fd, FrontendResponse& resp)
{
    ssize_t received = recv(fd, &resp, sizeof(resp), 0);
    if (received != sizeof(resp)) {
        if (received == 0) {
            std::cout << "Connection closed by platform" << std::endl;
        } else {
            perror("recv");
        }
        return false;
    }
    return true;
}

void print_response(const FrontendResponse& resp)
{
    // Check if it's a status update
    if (resp.response_len > 0) {
        std::string msg((char*)resp.response, resp.response_len);
        
        if (msg.find("\"type\":\"status_update\"") != std::string::npos) {
            // Parse JSON manually (simple parsing for demo)
            auto extract_int = [&](const std::string& key) -> int {
                size_t pos = msg.find("\"" + key + "\":");
                if (pos == std::string::npos) return 0;
                pos += key.length() + 3;
                return std::stoi(msg.substr(pos));
            };
            
            auto extract_bool = [&](const std::string& key) -> bool {
                size_t pos = msg.find("\"" + key + "\":");
                if (pos == std::string::npos) return false;
                return msg.find("true", pos) < msg.find(",", pos);
            };
            
            int pf_id = extract_int("pf_id");
            int traded_qty = extract_int("traded_qty");
            int achieved_spread = extract_int("achieved_spread");
            int current_spread = extract_int("current_spread");
            bool has_opportunity = extract_bool("has_opportunity");
            bool is_complete = extract_bool("is_complete");
            
            // Pretty print with colors (requires ANSI terminal)
            std::cout << "\n┌─────────────────────────────────────────────────" << std::endl;
            std::cout << "│ 🔔 LIVE UPDATE - Portfolio " << pf_id << std::endl;
            std::cout << "├─────────────────────────────────────────────────" << std::endl;
            
            // Current spread with color coding
            std::cout << "│ Current Spread:  ";
            if (has_opportunity) {
                std::cout << "\033[1;32m" << current_spread << " ✓ OPPORTUNITY\033[0m" << std::endl;
            } else {
                std::cout << current_spread << " (below threshold)" << std::endl;
            }
            
            std::cout << "│ Achieved Spread: " << achieved_spread << std::endl;
            std::cout << "│ Traded Qty:      " << traded_qty << std::endl;
            
            if (is_complete) {
                std::cout << "│ Status:          \033[1;32m✓ COMPLETE\033[0m" << std::endl;
            } else if (has_opportunity) {
                std::cout << "│ Status:          \033[1;33m⚡ EXECUTING\033[0m" << std::endl;
            } else {
                std::cout << "│ Status:          ⏳ WAITING FOR OPPORTUNITY" << std::endl;
            }
            
            std::cout << "└─────────────────────────────────────────────────\n" << std::endl;
            return;
        }
    }
    
    // Regular response (unchanged)
    std::cout << "\n┌─────────────────────────────────" << std::endl;
    std::cout << "│ Portfolio ID: " << resp.pf_id << std::endl;
    std::cout << "│ Status: " << (resp.status == 0 ? "✓ SUCCESS" : "✗ ERROR") << std::endl;
    
    if (resp.response_len > 0) {
        std::string msg((char*)resp.response, resp.response_len);
        std::cout << "│ Response: " << msg << std::endl;
    }
    std::cout << "└─────────────────────────────────\n" << std::endl;
}

// void print_response(const FrontendResponse& resp)
// {
//     std::cout << "\n┌─────────────────────────────────" << std::endl;
//     std::cout << "│ Portfolio ID: " << resp.pf_id << std::endl;
//     std::cout << "│ Status: " << (resp.status == 0 ? "✓ SUCCESS" : "✗ ERROR") << std::endl;
    
//     if (resp.response_len > 0) {
//         std::string msg((char*)resp.response, resp.response_len);
//         std::cout << "│ Response: " << msg << std::endl;
//     }
//     std::cout << "└─────────────────────────────────\n" << std::endl;
// }

/* ═══════════════════════════════════════════════════════════
 * COMMAND HANDLERS
 * ═══════════════════════════════════════════════════════════ */
template<typename T>
void read_packed(T& field) {
    T tmp;
    std::cin >> tmp;
    field = tmp;
}
void cmd_add_conrev_ioc(int fd, uint32_t pf_id)
{
    ConRevIOCParams params;
    
    std::cout << "\n=== Add ConRev IOC Strategy ===" << std::endl;
    uint32_t tmp; 
    std::cout << "Fut Token: ";std::cin >> tmp; params.fut_token = tmp;
    std::cout << "Call Token: ";    std::cin >> tmp;  params.call_token=tmp;
    std::cout << "Put Token: ";    std::cin >> tmp;  params.put_token=tmp;
    std::cout << "Strike Price: "; std::cin >> tmp;   params.strike_price=tmp;
    std::cout << "Max Lots: ";      std::cin >> tmp;  params.max_lots=tmp;
    std::cout << "SO: ";      std::cin >> tmp;  params.sol=tmp;
    std::cout << "Spread Threshold: ";std::cin >> tmp;params.spread_threshold=tmp;
    
    char conv;
    std::cout << "Is Conversion? (y/n): "; std::cin >> conv;
    params.is_conversion = (conv == 'y' || conv == 'Y');
    
    // Build command
    FrontendCommand cmd;
    cmd.cmd_type = FRONTEND_CMD_ADD;
    cmd.pf_id = pf_id;
    
    // Payload: [type_id][params]
    uint32_t type_id = TYPE_CONREV_IOC;
    memcpy(cmd.payload, &type_id, sizeof(uint32_t));
    memcpy(cmd.payload + sizeof(uint32_t), &params, sizeof(params));
    cmd.payload_len = sizeof(uint32_t) + sizeof(params);
    
    if (!send_command(fd, cmd)) return;
    
    FrontendResponse resp;
    if (recv_response(fd, resp)) {
        print_response(resp);
    }
}

// void cmd_add_conrev_bid(int fd, uint32_t pf_id)
// {
//     ConRevBidParams params;
    
//     std::cout << "\n=== Add ConRev Bidding Strategy ===" << std::endl;
//     std::cout << "Future Token: "; std::cin >> params.fut_token;
//     std::cout << "Call Token: "; std::cin >> params.call_token;
//     std::cout << "Put Token: "; std::cin >> params.put_token;
//     std::cout << "Strike Price: "; std::cin >> params.strike_price;
//     std::cout << "Max Lots: "; std::cin >> params.max_lots;
//     std::cout << "Spread Threshold: "; std::cin >> params.spread_threshold;
    
//     char conv;
//     std::cout << "Is Conversion? (y/n): "; std::cin >> conv;
//     params.is_conversion = (conv == 'y' || conv == 'Y');
    
//     char opp;
//     std::cout << "Is Opportunity? (y/n): "; std::cin >> opp;
//     params.is_opportunity = (opp == 'y' || opp == 'Y');
    
//     std::cout << "Leg1 Spread Threshold: "; std::cin >> params.leg1_spread_threshold;
//     std::cout << "Leg2 Timeout (us): "; std::cin >> params.leg2_timeout_us;
//     std::cout << "Leg3 Timeout (us): "; std::cin >> params.leg3_timeout_us;
    
//     FrontendCommand cmd;
//     cmd.cmd_type = FRONTEND_CMD_ADD;
//     cmd.pf_id = pf_id;
    
//     uint32_t type_id = TYPE_CONREV_BID;
//     memcpy(cmd.payload, &type_id, sizeof(uint32_t));
//     memcpy(cmd.payload + sizeof(uint32_t), &params, sizeof(params));
//     cmd.payload_len = sizeof(uint32_t) + sizeof(params);
    
//     if (!send_command(fd, cmd)) return;
    
//     FrontendResponse resp;
//     if (recv_response(fd, resp)) {
//         print_response(resp);
//     }
// }

// void cmd_add_box_bidding(int fd, uint32_t pf_id)
// {
//     BoxBiddingParams params;
    
//     std::cout << "\n=== Add Box Bidding Strategy ===" << std::endl;
//     std::cout << "ITM Call Token: "; std::cin >> params.itm_call_token;
//     std::cout << "OTM Put Token: "; std::cin >> params.otm_put_token;
//     std::cout << "OTM Call Token: "; std::cin >> params.otm_call_token;
//     std::cout << "ITM Put Token: "; std::cin >> params.itm_put_token;
//     std::cout << "Strike Difference: "; std::cin >> params.strike_diff;
//     std::cout << "Max Lots: "; std::cin >> params.max_lots;
//     std::cout << "Price Difference: "; std::cin >> params.price_difference;
    
//     char flip;
//     std::cout << "Flip Box Enabled? (y/n): "; std::cin >> flip;
//     params.flip_box_enabled = (flip == 'y' || flip == 'Y');
    
//     char opp;
//     std::cout << "Is Opportunity? (y/n): "; std::cin >> opp;
//     params.is_opportunity = (opp == 'y' || opp == 'Y');
    
//     std::cout << "Leg1 Spread Threshold: "; std::cin >> params.leg1_spread_threshold;
//     std::cout << "Leg1 Timeout (us): "; std::cin >> params.leg1_timeout_us;
//     std::cout << "Leg2 Timeout (us): "; std::cin >> params.leg2_timeout_us;
//     std::cout << "Leg3 Timeout (us): "; std::cin >> params.leg3_timeout_us;
//     std::cout << "Leg4 Timeout (us): "; std::cin >> params.leg4_timeout_us;
//     std::cout << "Entry Leg (1-4): "; 
//     int entry;
//     std::cin >> entry;
//     params.entry_leg = (uint8_t)entry;
    
//     FrontendCommand cmd;
//     cmd.cmd_type = FRONTEND_CMD_ADD;
//     cmd.pf_id = pf_id;
    
//     uint32_t type_id = TYPE_BOX_BIDDING;
//     memcpy(cmd.payload, &type_id, sizeof(uint32_t));
//     memcpy(cmd.payload + sizeof(uint32_t), &params, sizeof(params));
//     cmd.payload_len = sizeof(uint32_t) + sizeof(params);
    
//     if (!send_command(fd, cmd)) return;
    
//     FrontendResponse resp;
//     if (recv_response(fd, resp)) {
//         print_response(resp);
//     }
// }

// void cmd_add_box_ioc(int fd, uint32_t pf_id)
// {
//     BoxIOCParams params;
    
//     std::cout << "\n=== Add Box IOC Strategy ===" << std::endl;
//     std::cout << "ITM Call Token: "; std::cin >> params.call_itm_token;
//     std::cout << "OTM Put Token: "; std::cin >> params.put_otm_token;
//     std::cout << "OTM Call Token: "; std::cin >> params.call_otm_token;
//     std::cout << "ITM Put Token: "; std::cin >> params.put_itm_token;
//     std::cout << "Strike Difference: "; std::cin >> params.strike_diff;
//     std::cout << "Max Lots: "; std::cin >> params.max_lots;
//     std::cout << "Price Difference: "; std::cin >> params.price_difference;
    
//     char flip;
//     std::cout << "Is Flip Box? (y/n): "; std::cin >> flip;
//     params.is_flip_box = (flip == 'y' || flip == 'Y');
    
//     std::cout << "Timer (ms): "; std::cin >> params.timer_ms;
    
//     FrontendCommand cmd;
//     cmd.cmd_type = FRONTEND_CMD_ADD;
//     cmd.pf_id = pf_id;
    
//     uint32_t type_id = TYPE_BOX_IOC;
//     memcpy(cmd.payload, &type_id, sizeof(uint32_t));
//     memcpy(cmd.payload + sizeof(uint32_t), &params, sizeof(params));
//     cmd.payload_len = sizeof(uint32_t) + sizeof(params);
    
//     if (!send_command(fd, cmd)) return;
    
//     FrontendResponse resp;
//     if (recv_response(fd, resp)) {
//         print_response(resp);
//     }
// }

void cmd_edit(int fd, uint32_t pf_id, uint32_t type_id)
{
    std::cout << "\n=== Edit Strategy (pf_id=" << pf_id << ") ===" << std::endl;
    std::cout << "Note: Edit uses same params as Add (cannot change tokens/strike)" << std::endl;
    
    // For simplicity, just edit max_lots and spread_threshold
    // You can expand this to handle all params
    
    if (type_id == TYPE_CONREV_IOC) {
        ConRevIOCParams params;
        uint32_t tmp;
        std::cout << "Max Lots: ";      std::cin >> tmp;  params.max_lots=tmp;
    std::cout << "Spread Threshold: ";std::cin >> tmp;params.spread_threshold=tmp;
        // Keep other fields as-is (frontend should preserve them)
        
        FrontendCommand cmd;
        cmd.cmd_type = FRONTEND_CMD_EDIT;
        cmd.pf_id = pf_id;
        memcpy(cmd.payload, &params, sizeof(params));
        cmd.payload_len = sizeof(params);
        
        if (!send_command(fd, cmd)) return;
        
        FrontendResponse resp;
        if (recv_response(fd, resp)) {
            print_response(resp);
        }
    } else {
        std::cout << "Edit not implemented for this type. Use ADD command syntax." << std::endl;
    }
}

void cmd_run(int fd, uint32_t pf_id)
{
    FrontendCommand cmd;
    cmd.cmd_type = FRONTEND_CMD_RUN;
    cmd.pf_id = pf_id;
    cmd.payload_len = 0;
    
    if (!send_command(fd, cmd)) return;
    
    FrontendResponse resp;
    if (recv_response(fd, resp)) {
        print_response(resp);
    }
}

void cmd_stop(int fd, uint32_t pf_id)
{
    FrontendCommand cmd;
    cmd.cmd_type = FRONTEND_CMD_STOP;
    cmd.pf_id = pf_id;
    cmd.payload_len = 0;
    
    if (!send_command(fd, cmd)) return;
    
    FrontendResponse resp;
    if (recv_response(fd, resp)) {
        print_response(resp);
    }
}

void cmd_remove(int fd, uint32_t pf_id)
{
    FrontendCommand cmd;
    cmd.cmd_type = FRONTEND_CMD_REMOVE;
    cmd.pf_id = pf_id;
    cmd.payload_len = 0;
    
    if (!send_command(fd, cmd)) return;
    
    FrontendResponse resp;
    if (recv_response(fd, resp)) {
        print_response(resp);
    }
}

void cmd_query(int fd, uint32_t pf_id)
{
    FrontendCommand cmd;
    cmd.cmd_type = FRONTEND_CMD_QUERY;
    cmd.pf_id = pf_id;
    cmd.payload_len = 0;
    
    if (!send_command(fd, cmd)) return;
    
    FrontendResponse resp;
    if (recv_response(fd, resp)) {
        print_response(resp);
    }
}

/* ═══════════════════════════════════════════════════════════
 * HELP
 * ═══════════════════════════════════════════════════════════ */

void print_help()
{
    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║          HFT PLATFORM FRONTEND TEST CLIENT              ║
╠══════════════════════════════════════════════════════════╣
║ Commands:                                                ║
║                                                          ║
║  add <pf_id> <type>  - Add strategy                     ║
║      Types: conrev_ioc, conrev_bid, box_bid, box_ioc    ║
║                                                          ║
║  edit <pf_id> <type> - Edit strategy params             ║
║                                                          ║
║  run <pf_id>         - Start strategy execution         ║
║                                                          ║
║  stop <pf_id>        - Stop strategy execution          ║
║                                                          ║
║  remove <pf_id>      - Remove strategy                  ║
║                                                          ║
║  query <pf_id>       - Get strategy status              ║
║                                                          ║
║  help                - Show this help                   ║
║                                                          ║
║  quit                - Exit                             ║
║                                                          ║
╠══════════════════════════════════════════════════════════╣
║ Example Session:                                         ║
║                                                          ║
║  > add 0 conrev_ioc                                     ║
║  > run 0                                                ║
║  > query 0                                              ║
║  > stop 0                                               ║
║  > remove 0                                             ║
║                                                          ║
╚══════════════════════════════════════════════════════════╝
)" << std::endl;
}

/* ═══════════════════════════════════════════════════════════
 * REPL
 * ═══════════════════════════════════════════════════════════ */

void repl(int fd)
{
    std::string line;
    
    print_help();
    
    // Track active portfolios and their types (for edit)
    std::map<uint32_t, uint32_t> portfolio_types;
    
    while (true)
    {
        std::cout << "\nfrontend> ";
        std::getline(std::cin, line);
        
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        
        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            std::cout << "Exiting..." << std::endl;
            break;
        }
        
        if (cmd == "help" || cmd == "h" || cmd == "?") {
            print_help();
            continue;
        }
        
        if (cmd == "add") {
            uint32_t pf_id;
            std::string type;
            iss >> pf_id >> type;
            
            if (type == "conrev_ioc") {
                cmd_add_conrev_ioc(fd, pf_id);
                portfolio_types[pf_id] = TYPE_CONREV_IOC;
            }
            // else if (type == "conrev_bid") {
            //     cmd_add_conrev_bid(fd, pf_id);
            //     portfolio_types[pf_id] = TYPE_CONREV_BID;
            // }
            // else if (type == "box_bid" || type == "box_bidding") {
            //     cmd_add_box_bidding(fd, pf_id);
            //     portfolio_types[pf_id] = TYPE_BOX_BIDDING;
            // }
            // else if (type == "box_ioc") {
            //     cmd_add_box_ioc(fd, pf_id);
            //     portfolio_types[pf_id] = TYPE_BOX_IOC;
            // }
            else {
                std::cout << "Unknown type. Use: conrev_ioc, conrev_bid, box_bid, box_ioc" << std::endl;
            }
            continue;
        }
        
        if (cmd == "edit") {
            uint32_t pf_id;
            std::string type;
            iss >> pf_id >> type;
            
            uint32_t type_id = 0;
            if (type == "conrev_ioc") type_id = TYPE_CONREV_IOC;
            else if (type == "conrev_bid") type_id = TYPE_CONREV_BID;
            else if (type == "box_bid") type_id = TYPE_BOX_BIDDING;
            else if (type == "box_ioc") type_id = TYPE_BOX_IOC;
            
            cmd_edit(fd, pf_id, type_id);
            continue;
        }
        
        if (cmd == "run") {
            uint32_t pf_id;
            iss >> pf_id;
            cmd_run(fd, pf_id);
            continue;
        }
        
        if (cmd == "stop") {
            uint32_t pf_id;
            iss >> pf_id;
            cmd_stop(fd, pf_id);
            continue;
        }
        
        if (cmd == "remove") {
            uint32_t pf_id;
            iss >> pf_id;
            cmd_remove(fd, pf_id);
            portfolio_types.erase(pf_id);
            continue;
        }
        
        if (cmd == "query") {
            uint32_t pf_id;
            iss >> pf_id;
            cmd_query(fd, pf_id);
            continue;
        }
        
        std::cout << "Unknown command. Type 'help' for available commands." << std::endl;
    }
}

/* ═══════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <host> <port>" << std::endl;
        std::cerr << "Example: " << argv[0] << " localhost 8000" << std::endl;
        return 1;
    }
    
    const char* host = argv[1];
    uint16_t port = (uint16_t)atoi(argv[2]);
    
    int fd = connect_to_platform(host, port);
    if (fd < 0) {
        std::cerr << "Failed to connect to platform at " << host << ":" << port << std::endl;
        return 1;
    }
    
    std::cout << "\n✓ Connected to HFT Platform at " << host << ":" << port << std::endl;
    
    repl(fd);
    
    close(fd);
    return 0;
}
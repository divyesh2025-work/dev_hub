#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <iostream>
#include <x86intrin.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <thread>
#include <array>
#include <vector>
#include <random>


struct MarketData
{
    uint32_t token;
    std::array<uint32_t, 5> bids;
    std::array<uint32_t, 5> asks;
    std::array<uint32_t, 5> bid_qty;
    std::array<uint32_t, 5> ask_qty;
    uint32_t seqno;
    char msg_type;
    uint32_t internal_seqno;
    uint8_t stream_id;
    uint32_t ltp;
    long timestamp;
} __attribute__((packed));

struct FutureToken {
    uint32_t token;
    uint32_t cash_token;
    uint8_t streams;
    uint32_t expiry;
    const char* symbol;
    uint32_t base_price;
};

// === UPDATED STOCKS WITH FUTURE TOKENS ===
// SYMBOL         STREAM_ID   CASH_TOKEN  BASE_PRICE  ISSUED_CAP        IV_FACT     CAP_FACT       EXPIRY         FUTURE_TOKENS  STREAMS        
// ------------------------------------------------------------------------------------------------------------------------
// AUBANK         1           21238       881.00      745898230.00      0.75        2.40           1448548200     46472          16             
// AXISBANK       1           5900        1228.50     3102679760.00     0.92        1.00           1448548200     46477          16             
// BANKBARODA     1           4668        286.35      5171362179.00     0.36        2.40           1448548200     48579          15             
// CANBK          1           10794       139.24      9070651260.00     0.37        2.40           1448548200     48602          13             
// FEDERALBNK     2           1023        235.83      2460259693.00     1.00        2.40           1448548200     48636          18             
// HDFCBANK       2           1333        984.65      15360347948.00    0.99        0.73           1448548200     48652 .         13             
// ICICIBANK      2           4963        1320.40     7143176028.00     1.00        0.94           1448548200     48707          18             
// IDFCFIRSTB     2           11184       80.37       7338685073.00     0.90        2.40           1448548200     48734      ,    18             
// INDUSINDBK     2           5258        786.20      779075972.00      0.84        2.40           1448548200     48751          18             
// KOTAKBANK      3           1922        2083.20     1988592513.00     0.74        1.13           1448548200     48858    ,      15             
// PNB            3           10666       120.46      11492943268.00    0.30        2.40           1448548200     49067          14             
// SBIN           4           3045        960.75      9230617586.00     0.45        0.94           1448548200     49086          12             
// BANKNIFTY      -1          0           0.00        0.00              1.00        1.00           1448548200     37051          9              
// ------------------------------------------------------------------------------------------------------------------------
    // 4. Send message
 
//   ❌ Missing token t2: 87644
// [2025-12-29 17:47:05.791] [DEBUG]   ❌ Missing token t3: 87645
// [2025-12-29 17:47:05.791] [DEBUG] ⏳ Waiting for market data for strategyId: 3
// [2025-12-29 17:47:05.791] [DEBUG]   ❌ Missing token t2: 87650
// [2025-12-29 17:47:05.791] [DEBUG]   ❌ Missing token t3: 87651
// Future tokens from your data
// 49278, 49282, 49307, 49329, 49391, 49422, 49431, 49437, 49449, 49523, 49983, 50005, 50261, 50367

// SYMBOL         STREAM_ID   CASH_TOKEN  BASE_PRICE  ISSUED_CAP        IV_FACT     CAP_FACT       EXPIRY         FUTURE_TOKENS  STREAMS        
// ------------------------------------------------------------------------------------------------------------------------
// AUBANK         1           21238       1022.05     746932077.00      0.75        2.71           1456410600     59213          16             
// AXISBANK       1           5900        1377.00     3104488514.00     0.92        1.02           1456410600     59215          16             
// BANKBARODA     1           4668        305.15      5171362179.00     0.36        2.77           1456410600     59257          15             
// CANBK          1           10794       151.94      9070651260.00     0.37        2.69           1456410600     59283          13             
// FEDERALBNK     2           1023        290.85      2462945660.00     1.00        2.83           1456410600     59315          18             
// HDFCBANK       2           1333        924.70      15384577216.00    0.99        0.62           1456410600     59345          13             
// ICICIBANK      2           4963        1408.20     7150191757.00     1.00        0.75           1456410600     59353          18             
// IDFCFIRSTB     2           11184       84.61       8594892611.00     0.77        2.73           1456410600     59357          18             
// INDUSINDBK     2           5258        944.65      779075972.00      0.84        2.81           1456410600     59373          18             
// KOTAKBANK      3           1922        426.35      9945492975.00     0.74        1.18           1456410600     59397          15             
// PNB            3           10666       128.17      11492943268.00    0.30        2.92           1456410600     59450          14             
// SBIN           4           3045        1218.90     9230617586.00     0.45        0.93           1456410600     59466          12             
// UNIONBANK      4           10753       193.11      7633605607.00     0.25        1.93           1456410600     59499          17             
// YESBANK        4           11915       21.24       31377549600.00    0.56        1.82           1456410600     59520          18             
// BANKNIFTY      -1          0           0.00        0.00              1.00        1.00           1456410600     59175          9  
static const std::vector<FutureToken> FUTURE_TOKENS = {
    {59213, 21238, 16, 1443709800, "AUBANK", 96920},
    {46477, 5900, 16, 1443709800, "AXISBANK", 127900},
    {59215, 5900, 16, 1443709800, "AXISBANK", 131010},
    {59257, 4668, 15, 1443709800, "BANKBARODA", 28500},
    {49307, 4668, 15, 1443709800, "BANKBARODA", 30300},
    {59283, 10794, 13, 1443709800, "CANBK", 13600},
    {49329, 10794, 13, 1443709800, "CANBK", 15000},
    {59315, 1023, 18, 1443709800, "FEDERALBNK", 26000},
    {49391, 1023, 18, 1443709800, "FEDERALBNK", 26000}, 
    {99930, 1023, 18, 1443709800, "FEDERALBNK", 925},
    {99931, 1023, 18, 1443709800, "FEDERALBNK", 307},
    {99936, 1023, 18, 1443709800, "FEDERALBNK", 775},
    {99937, 1023, 18, 1443709800, "FEDERALBNK", 303},

    {87644, 1023, 18, 1443709800, "FEDERALBNK", 925},
    {59315, 1023, 18, 1443709800, "FEDERALBNK", 28180},
    {92629, 1023, 18, 1443709800, "FEDERALBNK", 925},
    {92638, 1023, 18, 1443709800, "FEDERALBNK", 925},
    {87645, 1023, 18, 1443709800, "FEDERALBNK", 307},
    {87650, 1023, 18, 1443709800, "FEDERALBNK", 775},
    {87651, 1023, 18, 1443709800, "FEDERALBNK", 303},

    {59345, 1333, 13, 1443709800, "HDFCBANK", 91500},
    {49422, 1333, 13, 1443709800, "HDFCBANK", 98900},
    {48707, 4963, 18, 1443709800, "ICICIBANK", 139000},
    {59353, 4963, 18, 1443709800, "ICICIBANK", 139000},
    {89309, 4963, 18, 1443709800, "ICICIBANK", 90},
    {89327, 4963, 18, 1443709800, "ICICIBANK", 1720},
    {89307, 4963, 18, 1443709800, "ICICIBANK", 340},
    {89305, 4963, 18, 1443709800, "ICICIBANK", 1500},
    {48734, 11184, 18, 1443709800, "IDFCFIRSTB", 8506},
    {59357, 11184, 18, 1443709800, "IDFCFIRSTB", 8184},
    {59373, 5258, 18, 1443709800, "INDUSINDBK", 90910},
    {49449, 5258, 18, 1443709800, "INDUSINDBK", 84500},
    {48858, 1922, 15, 1443709800, "KOTAKBANK", 214300},
    {59397, 1922, 15, 1443709800, "KOTAKBANK", 40955},
    {49067, 10666, 14, 1443709800, "PNB", 12300},
    {59450, 10666, 14, 1443709800, "PNB", 12200},
    {49086, 3045, 12, 1443709800, "SBIN", 102000},
    {50005, 3045, 12, 1443709800, "SBIN", 102000},
    {145360, 3045, 12, 1443709800, "SBIN", 1600},
    {59466, 3045, 12, 1443709800, "SBIN", 104500},
    {59499, 3045, 17, 1443709800, "UNIONBANK", 17171},
    {59520, 3045, 18, 1443709800, "YESBANK", 2133},
    {49085, 3045, 12, 1443709800, "SBILIFE", 197050},
    {145078, 3045, 12, 1443709800, "SBILIFE", 3960},
    {145079, 3045, 12, 1443709800, "SBILIFE", 4450},
    {52796, 0, 9, 1443709800, "BANKNIFTY", 4650}, // Index future
    {52952, 0, 9, 1443709800, "BANKNIFTY", 27010}, // Index future
    {52978, 0, 9, 1443709800, "BANKNIFTY", 32485}, // Index future
    {52787, 0, 9, 1443709800, "BANKNIFTY", 100000}, // Index future
    {52951, 0, 9, 1443709800, "BANKNIFTY", 30000}, // Index future
    {52932, 0, 9, 1443709800, "BANKNIFTY", 20000}, // Index future59175
    {59175, 0, 9, 1443709800, "BANKNIFTY", 6082600}, // Index future
    {49224, 0, 9, 1443709800, "BANKNIFTY", 5982000}, // Index future
    {92629, 0, 18, 1443709800, "FEDERAKBANK", 1100}, // Index future
    {92638, 0, 18, 1443709800, "FEDERAKBANK", 600} ,// Index future
    {92650, 0, 18, 1443709800, "FEDERAKBANK", 600}, // Index future
    {92658, 0, 18, 1443709800, "FEDERAKBANK", 1100} // Index future
};
// 92629 11, 92638 6, 92650 6, 92658 11
//  92629
 
// 92638
 
// 92650
 
// 92658
 
class FuturesMarketDataSender {
private:
    int multicast_socket;
    struct sockaddr_in multicast_addr;
    uint32_t sequence_number;
    std::mt19937 rng;
    std::uniform_real_distribution<double> price_variation;
    std::uniform_real_distribution<double> future_premium;
    std::array<int, 19> seq{};

    
    static inline void set_thread_affinity(uint64_t core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id % std::thread::hardware_concurrency(), &cpuset);
        pthread_t thread = pthread_self();
        pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
    }
    
    inline uint64_t rdtsc() {
        return __rdtsc();
    }

public:
    FuturesMarketDataSender() : sequence_number(1), rng(std::random_device{}()), 
                               price_variation(-0.03, 0.03), future_premium(1.005, 1.015) {}
    
    bool initialize(const char* multicast_group, int port, const char* interface_ip) {
        // Create UDP socket with optimizations
        multicast_socket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (multicast_socket < 0) {
            std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }

        // Set socket buffer sizes for low latency
        int sndbuf = 65536;
        int rcvbuf = 65536;
        setsockopt(multicast_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        setsockopt(multicast_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        // Enable broadcast/multicast
        int broadcast = 1;
        if (setsockopt(multicast_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
            std::cerr << "Failed to set broadcast option: " << strerror(errno) << std::endl;
            return false;
        }

        // Set multicast interface
        struct in_addr interface_addr;
        if (inet_aton(interface_ip, &interface_addr) == 0) {
            std::cerr << "Invalid interface IP address" << std::endl;
            return false;
        }

        if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) < 0) {
            std::cerr << "Failed to set multicast interface: " << strerror(errno) << std::endl;
            return false;
        }

        // Set TTL for multicast
        int ttl = 1;
        if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
            std::cerr << "Failed to set TTL: " << strerror(errno) << std::endl;
            return false;
        }

        // Disable loopback
        int loopback = 0;
        if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0) {
            std::cerr << "Failed to disable loopback: " << strerror(errno) << std::endl;
            return false;
        }

        // Set up destination address
        memset(&multicast_addr, 0, sizeof(multicast_addr));
        multicast_addr.sin_family = AF_INET;
        multicast_addr.sin_port = htons(port);
        if (inet_aton(multicast_group, &multicast_addr.sin_addr) == 0) {
            std::cerr << "Invalid multicast IP address" << std::endl;
            return false;
        }

        std::cout << "Futures Market Data Sender initialized on interface " << interface_ip
                  << " for multicast group " << multicast_group << ":" << port << std::endl;
        return true;
    }

    // void printData(MarketData& data)
    // {
    // std::cout << "MarketData for token 902354:\n";
    // std::cout << "Token: " << data.token << "\n";
    // std::cout << "Stream ID: " << static_cast<int>(data.stream_id) << "\n";
    // std::cout << "Sequence No: " << data.seqno << "\n";
    // std::cout << "Internal Seq No: " << data.internal_seqno << "\n";
    // std::cout << "Message Type: " << data.msg_type << "\n";
    // std::cout << "Timestamp: " << data.timestamp << "\n";
    // std::cout << "LTP: " << data.ltp << "\n";

    // std::cout << "Bids: ";
    // for (const auto& bid : data.bids) std::cout << bid << " ";
    // std::cout << "\n";

    // std::cout << "Asks: ";
    // for (const auto& ask : data.asks) std::cout << ask << " ";
    // std::cout << "\n";

    // std::cout << "Bid Quantities: ";
    // for (const auto& qty : data.bid_qty) std::cout << qty << " ";
    // std::cout << "\n";

    // std::cout << "Ask Quantities: ";
    // for (const auto& qty : data.ask_qty) std::cout << qty << " ";
    // std::cout << "\n";

    // }
    
    void generate_market_data(const FutureToken& token, MarketData& data) {
        data.token = token.token;
        data.stream_id = token.streams; // Futures typically use stream_id 1
        data.seqno = seq[static_cast<int>( data.stream_id)];
        seq[static_cast<int>( data.stream_id)]++;
        data.msg_type = 'N'; // Future
        data.internal_seqno = data.seqno;
        data.timestamp = rdtsc();
        
        uint32_t future_price;
        
        if (token.token == 52995) { // BANKNIFTY
            int variation = (rand() % 11 - 0.005) * 100; // ±500 paisa (₹5.00) in 100-paisa steps
            future_price = static_cast<uint32_t>(token.base_price );
        } else {
            int variation = (rand() % 11 - 0.005) * 100; // ±500 paisa
            int premium = (rand() % 6) * 100;        // 0 to ₹5.00 premium
            future_price = static_cast<uint32_t>(token.base_price +(variation )%10);
        }
        
        // Generate 5 levels of bid/ask with tighter spreads for futures
        for (int i = 0; i < 5; ++i) {
            data.bids[i] = future_price - i*1 ; // Each level 0.05 lower
            data.asks[i] = future_price  + i*1 ; // Each level 0.05 higher
            
            // Futures typically have larger quantities
            if (token.token == 49224) { // BANKNIFTY - index future
                data.bid_qty[i] = 105; // Smaller lot sizes for index
                data.ask_qty[i] = 105;
            } else {
                data.bid_qty[i] = 5000 + (rand()%5)*5000 + (i * 5000); // Stock futures
                data.ask_qty[i] = 5000 + (rand()%5)*5000 + (i * 5000);
            }
        }
        
        data.ltp = future_price;
        // if(token.token == 37051)
        // {

        //     std::cout<<"Future price of kotak: "<<future_price<<std::endl;
        //     std::cout<<"Future price of token.base_price: "<<token.base_price<<std::endl;
        // }

        // if(token.token == 12340)
        // {

        //     std::cout<<"Future price of kotak: "<<future_price<<std::endl;
        //     std::cout<<"Future price of token.base_price: "<<token.base_price<<std::endl;
        // }
        if(token.token == 49224)
        {

            std::cout<<"Future price of kotak: "<<future_price<<std::endl;
            std::cout<<"Future price of token.base_price: "<<token.base_price<<std::endl;
        }
        if (token.token == 99930)
        {
            std::cout << "Future price of: " << future_price << std::endl;
            std::cout << "Future price of token.base_price: " << token.base_price << std::endl;

            // Print all 5 bid/ask levels
            for (int i = 0; i < 5; ++i) {
                std::cout << "Level " << i+1 << ": "
                        << "Bid=" << data.bids[i] << " (Qty=" << data.bid_qty[i] << ")"
                        << " | Ask=" << data.asks[i] << " (Qty=" << data.ask_qty[i] << ")"
                        << std::endl;
            }
        }

        
        data.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
// {48707, 4963, 18, 1443709800, "ICICIBANK", 140000},
//     {89309, 4963, 18, 1443709800, "ICICIBANK", 2000},
//     {89327, 4963, 18, 1443709800, "ICICIBANK", 280},



        

    //         {52514, 3045, 12, 1443709800, "SBIN", 88740},
    // {12340, 3045, 12, 1443709800, "SBIN", 1600},
    // {123039, 3045, 12, 1443709800, "SBIN", 5900},
    }
    
    bool send_data(const MarketData& data) {
        ssize_t bytes_sent = sendto(multicast_socket, &data, sizeof(data), 0, 
                                   (sockaddr*)&multicast_addr, sizeof(multicast_addr));
        return bytes_sent == sizeof(data);
    }
    
    void run() {
        set_thread_affinity(3); // Use core 3 for futures data
        
        std::cout << "Starting Futures Market Data feed..." << std::endl;
        
        MarketData market_data;
        size_t token_index = 0;
        
        while (true) {
            // Cycle through all future tokens
            const FutureToken& current_token = FUTURE_TOKENS[token_index];
            
            // Generate and send market data
            generate_market_data(current_token, market_data);
            
            if (!send_data(market_data)) {
                std::cerr << "Failed to send market data for future token: " << current_token.token << std::endl;
            }
            
            // Move to next token
            token_index = (token_index + 1) % FUTURE_TOKENS.size();
            
            // Ultra low latency - slightly slower than cash for realistic behavior
            usleep(15000); // 150 microseconds between updates
        }
    }
    
    ~FuturesMarketDataSender() {
        if (multicast_socket >= 0) {
            close(multicast_socket);
        }
    }
};

int main() {
    FuturesMarketDataSender sender;
    
    // Configuration for futures market data
    const char* multicast_group = "239.1.1.1"; // Different from cash
    int port = 5000; // Different port for futures
    const char* interface_ip = "10.0.1.210";
    
    if (!sender.initialize(multicast_group, port, interface_ip)) {
        std::cerr << "Failed to initialize futures market data sender" << std::endl;
        return -1;
    }
    
    try {
        sender.run();
    } catch (const std::exception& e) {
        std::cerr << "Error in futures market data sender: " << e.what() << std::endl;
        return -1;
    }
    
    return 0;
}
// main.cpp
#include "Engine/HFTStrategyEngine.h"
#include "ConfigLoader/ConfigLoader.h"
#include <iostream>
#include <exception>
#include <chrono>
#include <thread>
#include <cmath>
#include <iostream>

void print_config(const Config &c)
{
    LOG_FILE("Config", "================ CONFIG ================");

    // Network
    LOG_FILE("Config", "[Network]");
    LOG_FILE("Config", "  frontend_port: " + std::to_string(c.frontend_port));
    LOG_FILE("Config", "  utrade_port: " + std::to_string(c.utrade_port));
    LOG_FILE("Config", "  market_port: " + std::to_string(c.market_port));
    LOG_FILE("Config", "  core: " + std::to_string(c.core));
    LOG_FILE("Config", std::string("  utrade_ip: ") + c.utrade_ip);
    LOG_FILE("Config", std::string("  market_ip: ") + c.market_ip);
    LOG_FILE("Config", std::string("  frontend_ip: ") + c.frontend_ip);
    LOG_FILE("Config", std::string("  interface_ip: ") + c.interface_ip);

    // Stream Sockets
    LOG_FILE("Config", "[StreamSockets]");
    LOG_FILE("Config", "  count: " + std::to_string(c.stream_socket_count));

    for (uint32_t i = 0; i < c.stream_socket_count && i < 6; i++)
    {
        const auto &s = c.stream_sockets[i];
        LOG_FILE("Config", "  socket[" + std::to_string(i) + "]:");
        LOG_FILE("Config", "    ip: " + std::string(s.ip));
        LOG_FILE("Config", "    port: " + std::to_string(s.port));
        LOG_FILE("Config", std::string("    auto_connect: ") + (s.auto_connect ? "true" : "false"));
    }

    // Performane
    LOG_FILE("Config", "[Performane]");
    LOG_FILE("Config", "  max_order_limit: " + std::to_string(c.max_order_limit));

    // Timing
    LOG_FILE("Config", "[Timing]");
    LOG_FILE("Config", "  market_interval_ms: " + std::to_string(c.market_interval_ms));

    // SHM / DB / Redis
    LOG_FILE("Config", "[SHM/DB]");
    LOG_FILE("Config", "  shm_log_name: " + c.shm_log_name);
    LOG_FILE("Config", "  shm_db_name: " + c.shm_db_name);
    LOG_FILE("Config", "  shm_log_1_name: " + c.shm_log_1_name);
    LOG_FILE("Config", "  db_conninfo: " + c.db_conninfo);
    LOG_FILE("Config", std::string("  redis_host: ") + c.redis_host);
    LOG_FILE("Config", "  redis_port: " + std::to_string(c.redis_port));

    LOG_FILE("Config", "========================================");
}

static void set_thread_affinity(uint64_t core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id % std::thread::hardware_concurrency(), &cpuset);
    pthread_t thread = pthread_self();
    pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
}

std::string getLogFileSHMName()
{
    char dateBuffer[16];
    std::time_t t = std::time(nullptr);
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y%m%d", std::localtime(&t));
    return "/strategy_log_" + std::string(dateBuffer);
}

int main()
{

    try
    {
        // loading config from env and setting things from env
        Config config = ConfigLoader::loadFromFile("../config/hft_config.env");

        print_config(config);
        set_thread_affinity(config.core);
        if (config.shm_log_name == "false")
        {
            config.shm_log_name = getLogFileSHMName();
        }

        // initialising class with given config
        HFTStrategyEngine engine(config);

        bool initialized = false;
        const int delayMs = 500;

        // seprate initialise function for doing connection to mUtrade, setting FS, Market data and Redis Recovery
        while (!initialized)
        {
            std::cerr << "Initialization attempt...\n";
            initialized = engine.initialize();

            if (!initialized)
            {
                std::cerr << "Initialization failed. Retrying in " << delayMs << "ms...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }

        std::cout << "Initialization succeeded.\n";
        std::cout << "HFT Strategy Engine started\n";

        // Starting While loop of code which continously listen to epoll
        engine.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown error occurred\n";
        return 1;
    }
    return 0;
}
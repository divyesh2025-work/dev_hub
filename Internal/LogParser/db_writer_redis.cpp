#include <iostream>
#include <atomic>
#include <thread>
#include <string>
#include <fstream>
#include <hiredis/hiredis.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <immintrin.h>
#include <map>
#include <vector>
#include <unordered_set>
#include "../Library/flat_hash_map.hpp"

#include "../Utils/db_redis.h"
#include "../ConfigLoader/ConfigLoader.h"
#include "../Include/Types.h"
// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        std::cerr << "Usage:\n"
                  << argv[0] << " dbwriter <host> <port>\n"
                  << argv[0] << " recovery <host> <port>\n";
        return 1;
    }

    Config config = ConfigLoader::loadFromFile("../config/hft_config.env");

    std::cout << config.shm_db_name << " ,******* core:" << config.core_db_writer << std::endl;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(config.core_db_writer, &cpuset); // Pin to core 8

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
    {
        std::cerr << "Failed to set process affinity" << std::endl;
    }
    else
    {
        std::cout << "Process pinned to core 8" << std::endl;
    }

    if (!strcmp(argv[1], "dbwriter"))
    {
        if (argc < 4)
        {
            std::cerr << "Usage: " << argv[0] << " dbwriter <host> <port>\n";
            return 1;
        }

        const char *host = argv[2];
        int port = std::stoi(argv[3]);

        db_writer_process(host, port, config.shm_db_name.c_str(), config.redis_db_index);
    }
    else
    {
        std::cerr << "Unknown command: " << argv[1] << "\n";
        return 1;
    }

    return 0;
}

// g++ -std=c++17 -O3 -march=native     db_writer_redis.cpp  ../ConfigLoader/ConfigLoader.cpp    -lhiredis     -lpthread     -o redis_db_test

// # Run DB writer
// ./redis_db_test dbwriter 127.0.0.1 6379

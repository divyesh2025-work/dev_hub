// // // ConfigLoader.cpp

#include "ConfigLoader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <cstring>

void ConfigLoader::trim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch)
                                    { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch)
                         { return !std::isspace(ch); })
                .base(),
            s.end());
}

bool ConfigLoader::parseBool(const std::string &val)
{
    return val == "true" || val == "1" || val == "yes";
}

Config ConfigLoader::loadFromFile(const std::string &filename)
{
    Config config{};
    std::ifstream file(filename);
    std::string line;

    while (std::getline(file, line))
    {
        trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        size_t equal_pos = line.find('=');
        if (equal_pos == std::string::npos)
            continue;

        std::string key = line.substr(0, equal_pos);
        std::string value = line.substr(equal_pos + 1);
        trim(key);
        trim(value);

        if (key == "UTRADE_IP")
            std::strncpy(config.utrade_ip, value.c_str(), sizeof(config.utrade_ip));
        else if (key == "UTRADE_PORT")
            config.utrade_port = std::stoi(value);
        else if (key == "MARKET_IP")
            std::strncpy(config.market_ip, value.c_str(), sizeof(config.market_ip));
        else if (key == "MARKET_PORT")
            config.market_port = std::stoi(value);
        else if (key == "INTERFACE_IP")
            std::strncpy(config.interface_ip, value.c_str(), sizeof(config.interface_ip));
        else if (key == "FRONTEND_SERVER_IP")
            std::strncpy(config.frontend_ip, value.c_str(), sizeof(config.frontend_ip));
        else if (key == "FRONTEND_SERVER_PORT")
            config.frontend_port = std::stoi(value);
        else if (key == "SHM_LOG_NAME")
            config.shm_log_name = value;
        else if (key == "SHM_DB_NAME")
            config.shm_db_name = value;
        else if (key == "SHM_LOG_1_NAME")
            config.shm_log_1_name = value;
        else if (key == "DB_CONNINFO")
            config.db_conninfo = value;
        else if (key == "REDIS_HOST")
            std::strncpy(config.redis_host, value.c_str(), sizeof(config.redis_host));
        else if (key == "REDIS_PORT")
            config.redis_port = std::stoi(value);
        else if (key == "REDIS_DB_INDEX")
            config.redis_db_index = std::stoi(value);
        else if (key == "CORE")
            config.core = std::stoi(value);
        else if (key == "CORE_DBWRITER")
            config.core_db_writer = std::stoi(value);
        else if (key == "MARKET_INTERVAL_MS")
            config.market_interval_ms = std::stoi(value);
        else if (key == "MAX_ORDER_LIMIT")
            config.max_order_limit = std::stoi(value);
        else if (key == "STREAM_SOCKET_COUNT")
            config.stream_socket_count = std::stoul(value);
        else
        {
            for (int i = 0; i < 6; ++i)
            {
                std::ostringstream ip_key, port_key, auto_key;
                ip_key << "STREAM_SOCKET_" << i << "_IP";
                port_key << "STREAM_SOCKET_" << i << "_PORT";
                auto_key << "STREAM_SOCKET_" << i << "_AUTO_CONNECT";

                if (key == ip_key.str())
                    std::strncpy(config.stream_sockets[i].ip, value.c_str(), sizeof(config.stream_sockets[i].ip));
                else if (key == port_key.str())
                    config.stream_sockets[i].port = std::stoi(value);
                else if (key == auto_key.str())
                    config.stream_sockets[i].auto_connect = parseBool(value);
            }
        }
    }

    return config;
}

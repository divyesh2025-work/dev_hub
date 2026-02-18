#pragma once
#include <cstdint>
#include <string>

struct Config
{
    // Network Config
    uint16_t frontend_port;
    uint16_t utrade_port;
    uint16_t market_port;
    uint16_t core;
    char utrade_ip[16];
    char market_ip[16];
    char frontend_ip[16];
    char interface_ip[16];

    // Stream Socket Configuration
    uint32_t stream_socket_count;
    struct StreamSocketConfig
    {
        char ip[16];
        uint16_t port;
        bool auto_connect;
    } stream_sockets[6];

    // Order limit
    int max_order_limit;

    // Timing Config
    int market_interval_ms;

    // SHM
    std::string shm_log_name;
    std::string shm_db_name;
    std::string shm_log_1_name;
    std::string db_conninfo;
    char redis_host[16];
    uint16_t redis_port;
    int redis_db_index;
    uint16_t core_db_writer;
};
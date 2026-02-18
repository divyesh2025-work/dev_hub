// SocketManager.h
#pragma once

#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <cmath>

#include <Include/core/Config.h>
#include <Include/core/Macros.h>
#include <Include/core/BasicTypes.h>
#include "Library/flat_hash_map.hpp"

class SocketManager
{
public:
    SocketManager(const Config &config);
    ~SocketManager();

    bool initialize_network();
    int getEpollFD() const { return epoll_fd; }
    int getFrontendSocket() const { return frontend_socket; }
    int getFrontendClientSocket() const { return frontend_client_socket; }
    void setFrontendClientSocket(int socket) { frontend_client_socket = socket; }
    int getUTradeSocket() const { return utrade_socket; }
    int getMarketSocket() const { return market_socket; }
    bool setupFrontendClient();
    bool reconnectUTrade();
    void cleanupResources();

private:
    Config config;
    int frontend_socket = -1;
    int utrade_socket = -1;
    int market_socket = -1;
    int epoll_fd = -1;
    int frontend_client_socket = -1;

    bool setupFrontendServer();
    bool setupUTrade();
    bool setupMarket();
    void setSocketOptions(int sock);

    //  map to store stream_id to socket file descriptor mapping
    ska::flat_hash_map<uint32_t, int> stream_socket_map;
};
// SocketManager.cpp
#include "SocketManager.h"

SocketManager::SocketManager(const Config &cfg) : config(cfg) {}

SocketManager::~SocketManager()
{

    cleanupResources();
}

void SocketManager::cleanupResources()
{
    if (epoll_fd != -1)
        close(epoll_fd);
    if (frontend_socket != -1)
        close(frontend_socket);
    if (utrade_socket != -1)
        close(utrade_socket);
    if (market_socket != -1)
        close(market_socket);
}

void SocketManager::setSocketOptions(int sockfd)
{

    int flag = 1;

    // Disable Nagle's algorithm
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Set socket buffer sizes
    int buf_size = 1024 * 1024*4; // 1MB//
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

    // Enable quickack
    setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));

    // // Set low latency
    int low_latency = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL, &low_latency, sizeof(low_latency));

    // Enable Reuse addresss
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // nonblocking for epoll
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
}

bool SocketManager::initialize_network()
{
    LOG_FILE("INIT", "Initializing SocketManager...");

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1)
    {
        LOG_FILE("INIT", "Failed to create epoll fd: " + std::string(strerror(errno)));
        return false;
    }

    // Step-by-step initialization with verbose logging
    if (!setupFrontendServer())
    {
        LOG_FILE("INIT", "Failed to setup frontend server");
        cleanupResources();
        return false;
    }

    if (!setupUTrade())
    {
        LOG_FILE("INIT", "Failed to setup UTrade socket");
        cleanupResources();
        return false;
    }

    if (!setupMarket())
    {
        LOG_FILE("INIT", "Failed to setup market socket");
        cleanupResources();
        return false;
    }

    return true;
}

bool SocketManager::setupFrontendClient()
{
    // std::cout<<"Came in seting up frontend client\n";
    //  if (event.data.fd == frontend_socket) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(frontend_socket, (sockaddr *)&client_addr, &client_len);
    if (client_fd >= 0)
    {
        fcntl(client_fd, F_SETFL, O_NONBLOCK);
        frontend_client_socket = client_fd;

        epoll_event client_event{};
        client_event.events = EPOLLIN | EPOLLET;
        // client_event.data.fd = client_fd;
        client_event.data.u64 = static_cast<uint64_t>(FDTag::FrontendClient);
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);

        std::cout << "Frontend client connected\n";
        // }
        return true;
    }

    return false;
}
bool SocketManager::setupFrontendServer()
{
    frontend_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (frontend_socket == -1)
        return false;

    setSocketOptions(frontend_socket);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.frontend_port);
    inet_pton(AF_INET, config.frontend_ip, &addr.sin_addr);

    if (bind(frontend_socket, (sockaddr *)&addr, sizeof(addr)) == -1)
    {
        std::cerr << "Bind failed frontend server: " << strerror(errno) << "\n";
        return false;
    }

    if (listen(frontend_socket, 1) == -1)
    {
        std::cerr << "Listen failed frontend server: " << strerror(errno) << "\n";
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    // event.data.fd = frontend_socket;
    event.data.u64 = static_cast<uint64_t>(FDTag::FrontendSocket);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, frontend_socket, &event) == -1)
    {
        std::cerr << "Epoll add failed frontend server: " << strerror(errno) << "\n";
        return false;
    }
    std::cout << "Frontend Server Up" << std::endl;

    return true;
}

bool SocketManager::reconnectUTrade()
{
    const int delayMs = 500; // fixed delay: 500ms
    int attempt = 0;

    while (true)
    {
        LOG_FILE("RECONNECT", "Attempt " + std::to_string(attempt + 1) + " to reconnect UTrade");

        // Close socket if needed
        if (utrade_socket != -1)
        {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, utrade_socket, nullptr) == -1)
            {
                LOG_FILE("RECONNECT", "Failed to remove socket from epoll: " + std::string(strerror(errno)));
            }
            close(utrade_socket);
            utrade_socket = -1;
        }

        // Try to reconnect
        if (setupUTrade())
        {
            LOG_FILE("RECONNECT", "Successfully reconnected UTrade, socket: " + std::to_string(utrade_socket));
            return true;
        }

        // Failed to reconnect – wait and retry
        LOG_FILE("RECONNECT", "Reconnect failed. Waiting " + std::to_string(delayMs) + "ms before retry...");
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        attempt++;
    }

    // This line is technically unreachable
    return false;
}

bool SocketManager::setupUTrade()
{
    utrade_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (utrade_socket == -1)
        return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.utrade_port);
    inet_pton(AF_INET, config.utrade_ip, &addr.sin_addr);

    if (connect(utrade_socket, (sockaddr *)&addr, sizeof(addr)) == -1 && errno != EINPROGRESS)
    {
        std::cerr << "Connect failed for utrade: " << strerror(errno) << "\n";
        return false;
    }
    setSocketOptions(utrade_socket);

    epoll_event event{};
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    // event.data.fd = utrade_socket;
    event.data.u64 = static_cast<uint64_t>(FDTag::UTrade);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, utrade_socket, &event) == -1)
    {
        std::cerr << "Epoll add failed for utrade: " << strerror(errno) << "\n";
        return false;
    }
    std::cout << "Utrade connected its fd is :" << utrade_socket << std::endl;

    return true;
}

bool SocketManager::setupMarket()
{
    market_socket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (market_socket == -1)
    {
        std::cerr << "Socket creation failed for market: " << strerror(errno) << "\n";
        return false;
    }

    // 1. Set socket options BEFORE binding
    int flag = 1;
    if (setsockopt(market_socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
    {
        std::cerr << "SO_REUSEADDR failed for market: " << strerror(errno) << "\n";
        close(market_socket);
        return false;
    }

    // 2. Enable SO_REUSEPORT for better multicast handling
    if (setsockopt(market_socket, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) < 0)
    {
        std::cerr << "Warning: SO_REUSEPORT failed for market: " << strerror(errno) << "\n";
        // Not critical, continue
    }

    // 3. Increase buffer sizes significantly for high-frequency data
    int rcvbuf_size = 128 * 1024 * 1024; // 50 MB for 10k updates/sec
    if (setsockopt(market_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0)
    {
        std::cerr << "Warning: Failed to set receive buffer size for market: " << strerror(errno) << "\n";
    }

    // 4. Also set send buffer (even for receive-only sockets, helps with ICMP handling)
    int sndbuf_size = 10 * 1024 * 1024; // 10 MB
    if (setsockopt(market_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size)) < 0)
    {
        std::cerr << "Warning: Failed to set send buffer size for market: " << strerror(errno) << "\n";
    }

    // 5. Set socket timeout to prevent indefinite blocking
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(market_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        std::cerr << "Warning: Failed to set receive timeout for market: " << strerror(errno) << "\n";
    }

    // 6. Disable multicast loop if you don't need to receive your own packets
    int loop = 0;
    if (setsockopt(market_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
    {
        std::cerr << "Warning: Failed to disable multicast loop for market: " << strerror(errno) << "\n";
    }

    // 7. Set multicast TTL (optional, but good practice)
    int ttl = 32;
    if (setsockopt(market_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0)
    {
        std::cerr << "Warning: Failed to set multicast TTL for market: " << strerror(errno) << "\n";
    }

    // Note: Non-blocking flag is already set during socket creation with SOCK_NONBLOCK
    // But keeping this as backup (redundant but harmless)
    int flags = fcntl(market_socket, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(market_socket, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(config.market_port);

    if (bind(market_socket, (sockaddr *)&addr, sizeof(addr)) == -1)
    {
        std::cerr << "Market bind failed : " << strerror(errno) << "\n";
        close(market_socket);
        return false;
    }

    // 8. Join multicast group with proper error handling
    struct ip_mreq mreq;
    if (inet_aton(config.market_ip, &mreq.imr_multiaddr) == 0)
    {
        std::cerr << "Invalid multicast IP for market: " << config.market_ip << "\n";
        close(market_socket);
        return false;
    }
    if (inet_aton(config.interface_ip, &mreq.imr_interface) == 0)
    {
        std::cerr << "Invalid interface ip IP for market: " << config.interface_ip << "\n";
        close(market_socket);
        return false;
    }

    if (setsockopt(market_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        std::cerr << "Joining multicast group failed market: " << strerror(errno) << "\n";
        std::cerr << "Group: " << config.market_ip << ", Interface: " << config.interface_ip << "\n";
        close(market_socket);
        return false;
    }

    // // 9. Add to epoll with proper error handling
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET; // Edge-triggered
    event.data.u64 = static_cast<uint64_t>(FDTag::Market);

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, market_socket, &event) == -1)
    {
        std::cerr << "Epoll add failed for market: " << strerror(errno) << "\n";
        // Leave multicast group before closing
        setsockopt(market_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        close(market_socket);
        return false;
    }

    std::cout << "Market socket setup complete and joined to multicast group: "
              << config.market_ip << ":" << config.market_port << "\n";

    return true;
}
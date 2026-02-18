#include <iostream>
#include <fstream>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <set>
#include <map>
 
#pragma pack(push,1)
struct MarketData
{
    uint32_t token;
    uint32_t bids[5];
    uint32_t asks[5];
    uint32_t bid_qty[5];
    uint32_t ask_qty[5];
    uint32_t seqno;
    char msg_type;
    uint32_t internal_seqno;
    uint8_t stream_id;
    uint32_t ltp;
    long changed_level;
};
#pragma pack(pop)
void udpHandler()
{
  std::ofstream ofs("market_data_log1.csv", std::ios::app);
  const char *multicast_group = "239.255.1.2";
  const int multicast_port = 50002;
 
  int sockfd = socket(AF_INET, SOCK_DGRAM | O_NONBLOCK, 0);
  if (sockfd < 0)
  {                                                      
    std::cerr << "Error creating sockt" << std::endl;
    return;
  }
 
   // Set socket buffer sizes (add these lines)
  int rcvbuf_size = 10 * 1024 * 1024; // 10MB receive buffer (adjust as needed)
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0)
  {
      std::cerr << "Warning: Failed to set receive buffer size (" << strerror(errno) << ")\n";
      // Continue anyway - the kernel will use its default size
  }
 
  // int busy_poll_us = 40;
  // setsockopt(sockfd, SOL_SOCKT, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));
 
  // Allow multiple sockets to use the same port
  int reuse = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "Error setting SO_REUSEADDR" << std::endl;
    close(sockfd);
    return;
  }
 
  // Bind to the appropriate port
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(multicast_port);
 
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)))
  {
    std::cerr << "Error binding socket" << std::endl;
    close(sockfd);
    return;
  }
 
  // Join the multicast group
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr = inet_addr(multicast_group);
  mreq.imr_interface.s_addr = inet_addr("10.180.5.5");
  if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
  {
    std::cerr << "Error joining multicast group" << std::endl;
    close(sockfd);
    return;
  }
 
  // Set socket to non-blocking (optional)
  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
 
  MarketData market_data;
  struct sockaddr_in src_addr;
  socklen_t addrlen = sizeof(src_addr);
 std::unordered_map<int, int>m1;
 std::cout<<"hiiiiii"<<'\n';
  const std::vector<int> default_subscriptions = {57113, 125311, 125312, 56929, 56928, 56954, 56957, 57088, 56900, 56908, 64397, 64403, 64400, 64404, 64393, 64395, 64388, 54793, 54794, 57915, 57916, 57032, 57033, 57042, 57034, 57035, 57024, 57026, 57021, 57020, 57043, 53216, 56785, 53224, 53235, 53250, 53251, 53252, 53256, 53257, 53258,53259, 53260, 53261, 53265, 53273, 53277, 53278, 53295, 53296, 102682, 102681, 124453, 134979, 134980, 124450, 57105, 56995, 57070, 124588, 124587, 57248, 43300, 43291, 57254, 144600, 144601, 56968, 100155, 100156, 57238, 130005, 130006, 57222, 57223, 57224, 57225, 57239, 57251, 57261, 57263, 57264, 64454, 40031, 40030, 40023, 40022, 40021, 40020, 40024, 54079, 54080, 54076, 54078, 54081, 54082, 54083, 54086, 54049, 54050, 79372, 79371, 79373, 79374, 53213, 56907, 57849, 57850, 57851, 57852, 58034, 58682, 58683, 58684, 58688, 58687, 40038, 40039,40040, 40041, 47267, 47268, 47269, 47270, 47273, 47274, 47275, 47276, 47277, 54045, 54046, 54047, 54048, 54049, 54050, 54051, 54053, 54054, 54056, 54059, 54060, 54061, 54062, 54063, 54066, 54067, 54068};
  std::vector<int>v1;
  std::map<uint8_t,std::pair<uint32_t, uint32_t>> j1,j2;
  j1[0] = {0,0};
  j1[1] = {0,0};
  j2[0] = {0,0};
  j2[1] = {0,0};
  int q = 0;
  while (true)
  {
 
    ssize_t nbytes = recv(sockfd,  &market_data, sizeof(MarketData), 0);
 
    if (nbytes == -1)
    {
      continue;
    }
    m1[market_data.token] = 1;
    // std::cout<< market_data.token<<'\n';
//     if(market_data.token == 141145 || market_data.token == 141144 || market_data.token == 65222)
//     {
//       if(market_data.msg_type == 'N' || market_data.msg_type == 'M')
//       {
// if (ofs) {
//     ofs << market_data.token << ',';
 
//     for (int i = 0; i < 5; ++i) ofs << market_data.bids[i] << ',';
//     for (int i = 0; i < 5; ++i) ofs << market_data.asks[i] << ',';
//     for (int i = 0; i < 5; ++i) ofs << market_data.bid_qty[i] << ',';
//     for (int i = 0; i < 5; ++i) ofs << market_data.ask_qty[i] << ',';
 
//     ofs << market_data.seqno << ',';
//     ofs << market_data.msg_type << '\n';asks 
// }
 
 
      // }
    // }
    // std::cout<< market_data.seqno<<" "<<market_data.msg_type<<'\n';
    if(static_cast<int>( market_data.stream_id) ==9)
    {
                std::cout<<market_data.token<<","<<market_data.ltp<<","<<market_data.changed_level <<"," <<static_cast<int>( market_data.stream_id)<<","<<market_data.internal_seqno << ","<<market_data.seqno<<","<<market_data.msg_type <<std::endl;
      // if(market_data.bids[0] == 0 || market_data.asks[0] == 0)
      // {
        // std::cout<<"-"<<std::endl;
        // std::cout<< market_data.changed_level<<'\n';
        // std::cout<< market_data.ltp<<'\n';
        // std::cout<< market_data.internal_seqno<<" "<<market_data.token<<'\n';
          std::map<uint8_t, std::pair<uint32_t, uint32_t>> s1,s2;
      for(int i=0;i<2;i++)
      {
            //  std::cout<<market_data.bids[i] <<std::endl;
            // std::cout<<market_data.bids[i]<<" "<<market_data.bid_qty[i]<<std::endl;
            s1[i] = {market_data.bids[i], market_data.bid_qty[i]};
      }
            //   std::cout<<"asks "<<std::endl;
      for(int i=0;i<2;i++)
      {
        // std::cout<<market_data.asks[i]<<'\n';
            //  std::cout<<market_data.asks[i]<<" "<<market_data.ask_qty[i]<<std::endl;
             s2[i] = {market_data.asks[i], market_data.ask_qty[i]};
      }
      int p = 0;
      if(j1[0].first == s1[0].first && j1[0].second == s1[0].second && j1[1].second == s1[1].second && j1[1].first == s1[1].first)
      {
        p++;
      }
      if(j2[0].first == s2[0].first && j2[0].second == s2[0].second && j2[1].second == s2[1].second && j2[1].first == s2[1].first)
      {
        p++;
      }
      if(p==2)
      {
        if(market_data.msg_type == 'T')
        {
        // std::cout<<"hiii "<< market_data.msg_type<< " "<<static_cast<int>(market_data.seqno) <<'\n';
        break;
        }
      }
      j1[0].first = s1[0].first;
            j1[0].second = s1[0].second;
      j1[1].first = s1[1].first;
      j1[1].second = s1[1].second;
      j2[0].first = s2[0].first;
      j2[0].second = s2[0].second;
      j2[1].first = s2[1].first;
      j2[1].second = s2[1].second;
      q = 0;
      if(market_data.msg_type == 'T')
      {
        q=1;
      }
    //   std::cout<< market_data.msg_type<< " "<< static_cast<int>(market_data.seqno)<<'\n';
      // std::cout<< (market_data.changed_level)<<'\n';
    // }
    }
    // std::cout<< market_data.token<<'\n';
    //  std::cout<< m1.size()<<'\n';
  }
  // for(auto it:m1)
  // {
  //   std::cout<< it.first<<'\n';
  // }
  close(sockfd);
}
 
int main() {
    udpHandler();
    return 0;
}
 
 
 
 
 
 
 
 
 
// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <unistd.h>
// #include <fstream>
// #include <cstring>
// #include <iostream>
// #include <x86intrin.h>
// #include <sched.h>
// #include <pthread.h>
// #include <unistd.h>
// #include <thread>
// #include <array>
// #include <fcntl.h>
 
// struct MarketData {
//     uint32_t token;
//     std::array<uint32_t, 5> bids;
//     std::array<uint32_t, 5> asks;
//     std::array<uint32_t, 5> bid_qty;
//     std::array<uint32_t, 5> ask_qty;
// };
 
// static void set_thread_affinity(uint64_t core_id) {
//     cpu_set_t cpuset;
//     CPU_ZERO(&cpuset);
//     CPU_SET(core_id % std::thread::hardware_concurrency(), &cpuset);
//     pthread_t thread = pthread_self();
//     pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
// }
 
// int main() {
//     set_thread_affinity(27);
   
//     const char* multicast_group = "239.255.1.2";
//     int port = 50002;
//     const char* interface_ip = "10.180.2.16";  // Specific interface to listen on
   
//     // Create UDP socket
//     int sock = socket(AF_INET, SOCK_DGRAM, 0);
//     if (sock < 0) {
//         perror("Socket creation failed");
//         return -1;
//     }
   
//     // Allow multiple processes to bind to the same port (for multicast)
//     int reuse = 1;
//     if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
//         perror("Failed to set SO_REUSEADDR");
//         close(sock);
//         return -1;
//     }
   
//     // Bind to the multicast port on all interfaces
//     sockaddr_in local_addr{};
//     local_addr.sin_family = AF_INET;
//     local_addr.sin_port = htons(port);
//     local_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to all interfaces initially
   
//     if (bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
//         perror("Bind failed");
//         close(sock);
//         return -1;
//     }
   
//     // Set up multicast group membership request for specific interface
//     struct ip_mreqn mreq{};
   
//     // Multicast group address
//     if (inet_pton(AF_INET, multicast_group, &mreq.imr_multiaddr) <= 0) {
//         perror("Invalid multicast group address");
//         close(sock);
//         return -1;
//     }
   
//     // Specific interface address to listen on
//     if (inet_pton(AF_INET, interface_ip, &mreq.imr_address) <= 0) {
//         perror("Invalid interface IP address");
//         close(sock);
//         return -1;
//     }
   
//     // Interface index (optional, can be 0)
//     mreq.imr_ifindex = 0;
   
//     // Join the multicast group on the specific interface
//     if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
//         perror("Failed to join multicast group on specified interface");
//         close(sock);
//         return -1;
//     }
//      int flags = fcntl(sock, F_GETFL, 0);
//    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
   
//     std::cout << "Listening for multicast on " << multicast_group
//               << ":" << port << " via interface " << interface_ip << std::endl;
   
//     std::ofstream log("multicast_receive_log.txt");
//     MarketData received_data;
//     sockaddr_in sender_addr{};
//     socklen_t sender_len = sizeof(sender_addr);
   
//     while(1) {
//         // Receive multicast data
//         // std::cout<<"hiii"<<'\n';
//         ssize_t bytes_received = recvfrom(sock, &received_data, sizeof(received_data), 0,
//                                          (sockaddr*)&sender_addr, &sender_len);
       
//         if (bytes_received < 0) {
//               continue;
//         }
//         std::cout<< bytes_received<<'\n';
//         if (bytes_received == sizeof(MarketData)) {
//             // Log received data
//             char sender_ip[INET_ADDRSTRLEN];
//             inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
           
//             std::cout << "Received from " << sender_ip << ":" << ntohs(sender_addr.sin_port)
//                       << " - Token: " << received_data.token
//                       << ", First Ask: " << received_data.asks[0]
//                       << ", First Bid: " << received_data.bids[0] << std::endl;
           
//             log << "Token: " << received_data.token << " From: " << sender_ip << std::endl;
//         } else {
//             std::cout << "Received incomplete data: " << bytes_received << " bytes" << std::endl;
//         }
//     }
   
//     // Leave the multicast group
//     if (setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
//         perror("Failed to leave multicast group");
//     }
   
//     log.close();
//     close(sock);
//     return 0;
// }
 
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

int main()
{
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        return 1;
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        return 1;
    }

    // Bind socket to port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    address.sin_port = htons(8755);       // Port 8752

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    // Listen for connections
    if (listen(server_fd, 3) < 0)
    {
        perror("Listen failed");
        return 1;
    }

    std::cout << "Server listening on port 8080..." << std::endl;

    // Accept a client connection
    if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
    {
        perror("Accept failed");
        return 1;
    }

    std::cout << "Client connected!" << std::endl;

    while (true)
    {
        // Read data from client

        int valread = read(client_fd, buffer, 1024);
    }
    std::cout << "Received: " << buffer << std::endl;

    // Send response
    // const char *response = "Hello from server!";
    // send(client_fd, response, strlen(response), 0);
    // std::cout << "Response sent" << std::endl;

    // Close sockets
    close(client_fd);
    close(server_fd);

    return 0;
}
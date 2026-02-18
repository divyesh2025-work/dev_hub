#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include "../Include/Types.h"

void sendEditMessage(int sock, int portfolio_id)
{
    FrontendMessage edit_msg = {};
    edit_msg.msg_type = FrontendMessageType::Edit;
    edit_msg.portfolio_id = portfolio_id;

    // Set strategy parameters for editing
    edit_msg.edit.params = {
        .max_lots = 5000,
        .sol = 1800,
        .forward_spread = 100,
        .revers_spread = -2000,
        .opp_check = 1,
        .diff = 3000,
        .timer = 60000000};

    ssize_t sent = send(sock, &edit_msg, sizeof(edit_msg), 0);
    if (sent < 0)
    {
        perror("Send EDIT failed");
    }
    else
    {
        std::cout << "EDIT message sent (" << sent << " bytes)\n";
    }
}

int main()
{
    // socket connection code...

    // Send ADD first (as you already do)

    // sleep(1); // allow time for server to process ADD

    // Send EDIT message for the same portfolio
    sendEditMessage(sock, 5);

    // Keep running or clean up
    while (true)
    {
    }

    close(sock);
    return 0;
}

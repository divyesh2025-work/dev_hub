#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <iostream>
#include "/home/dev_3/StrategyAllProcess/Include/frontend/FrontendMessages.h"
int main()
{
   
    std::cout<<"Size :" <<sizeof(FrontendMessage)<<std::endl;
    return 0;
}
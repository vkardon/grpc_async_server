//
// main.cpp
//
#include <stdio.h>
#include "testServer.hpp"
#include "logger.hpp"

int main(int argc, char *argv[])
{
    unsigned short port = 50050;
    int threadCount = 8;

    // Build & start gRpc server
    TestServer srv;
    srv.Run(port, threadCount);

    INFOMSG_MT("Grpc Server has stopped");
    return 0;
}


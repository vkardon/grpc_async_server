//
// main.cpp
//
#include <stdio.h>
#include "testServer.h"
#include "logger.h"

int main(int argc, char *argv[])
{
    unsigned short port = 50051;
    int threadCount = 8;

    // Build & start gRpc server
    TestServer srv;
    srv.Run(port, threadCount, true /*enableReflection*/);

    INFOMSG_MT("Grpc Server has stopped");
    return 0;
}


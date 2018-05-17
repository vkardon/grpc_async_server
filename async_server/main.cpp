//
// main.cpp
//
#include <stdio.h>
#include "grpcServer.h"
#include "logger.h"

int main(int argc, char *argv[])
{
    unsigned short port = 50051;
    int threadCount = 8;

    bool res = StartGrpcServer(port, threadCount);

    INFOMSG_MT("Grpc Server has stopped");

    return (res ? 0 : 1);
}


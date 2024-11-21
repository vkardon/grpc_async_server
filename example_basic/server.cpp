//
// server.cpp
//
#include <stdio.h>
#include "serverConfig.hpp"  // for PORT_NUMBER
#include "server.hpp"

int main(int argc, char* argv[])
{
    // Build & start gRpc server.
    MyServer srv;
    srv.Run(PORT_NUMBER, 8 /*number of threads*/);

    std::cout << "Grpc Server has stopped" << std::endl;
    return 0;
}


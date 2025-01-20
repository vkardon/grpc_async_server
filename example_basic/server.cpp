//
// server.cpp
//
#include <stdio.h>
#include "grpcServer.hpp"
#include "helloService.hpp"
#include "serverConfig.hpp"  // for PORT_NUMBER

class MyServer : public gen::GrpcServer
{
public:
    MyServer()
    {
        // Add all services
        AddService<HelloService>();
    }
    virtual ~MyServer() = default;
};

int main(int argc, char* argv[])
{
    // Build & start gRpc server.
    MyServer srv;
    srv.Run(PORT_NUMBER, 8 /*number of threads*/);

    std::cout << "Grpc Server has stopped" << std::endl;
    return 0;
}


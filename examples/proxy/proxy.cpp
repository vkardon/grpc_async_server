//
// server.cpp
//
#include <stdio.h>
#include "grpcServer.hpp"
#include "helloServiceProxy.hpp"
#include "serverConfig.hpp"  // for PORT_NUMBER

class MyProxy : public gen::GrpcServer
{
public:
    MyProxy(const std::string& forwardHost, unsigned short forwardPort)
    {
        // Add all services
        AddService<HelloServiceProxy>(forwardHost, forwardPort);
    }
    virtual ~MyProxy() = default;

    virtual bool OnInit(::grpc::ServerBuilder& /*builder*/) override
    {
        return true;
    }
};

int main(int argc, char* argv[])
{
    // Build & start gRpc server.
    MyProxy srv(FORWARD_HOST, FORWARD_PORT);
    srv.Run(PROXY_PORT, 8 /*number of threads*/);

    std::cout << "Grpc Server has stopped" << std::endl;
    return 0;
}


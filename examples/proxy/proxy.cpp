//
// server.cpp
//
#include <stdio.h>
#include "grpcServer.hpp"
#include "helloServiceProxy.hpp"
#include "controlService.hpp"
#include "serverConfig.hpp"     // for PORT_NUMBER
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.

class MyProxy : public gen::GrpcServer
{
public:
    MyProxy(const std::string& forwardHost, unsigned short forwardPort)
    {
        // Add all services
        AddService<HelloServiceProxy>(forwardHost, forwardPort);
        AddService<ControlService>();
    }
    virtual ~MyProxy() = default;

private:
    // GrpcServer overrides
    virtual bool OnInit(::grpc::ServerBuilder& builder) override
    {
        builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
        return true;
    }

    // Error/Info messages produced by gen::GrpcServer
    virtual void OnError(const std::string& err) const override { ERRORMSG(err); }
    virtual void OnInfo(const std::string& info) const override { INFOMSG(info); }
};

int main(int argc, char* argv[])
{
    // Build & start gRpc server.
    MyProxy srv(FORWARD_HOST, FORWARD_PORT);
    srv.Run(PROXY_PORT, 8 /*number of threads*/);

    INFOMSG("Grpc Proxy Server has stopped");
    return 0;
}


//
// router.cpp
//
#include <stdio.h>
#include "grpcServer.hpp"
#include "helloServiceRouter.hpp"
#include "controlService.hpp"
#include "serverConfig.hpp"     // for PORT_NUMBER
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.

class MyRouter : public gen::GrpcServer
{
public:
    MyRouter(const std::string& targetHost, unsigned short targetPort)
    {
        // Add all services
        AddService<HelloService>(targetHost, targetPort);
        AddService<ControlService>();
    }
    virtual ~MyRouter() = default;

private:
    // GrpcServer overrides
    virtual bool OnInit(::grpc::ServerBuilder& builder) override
    {
        builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

//        // Example: Set the maximum message size for both inbound and outbound messages
//        builder.SetMaxReceiveMessageSize(INT_MAX);
//        builder.SetMaxSendMessageSize(INT_MAX);
//
//        // Example: Limit memory and thread usage by the gRPC library
//        // ResourceQuota represents a bound on memory and thread usage by the gRPC
//        // library. gRPC will attempt to keep memory and threads used by all attached
//        // entities below the ResourceQuota bound.
//        grpc::ResourceQuota quota;
//        quota.Resize(1024 * 1024 * 10); // 10MB memory max
//        builder.SetResourceQuota(quota);
//        //builder.SetMaxThreads(int new_max_threads);

        return true;
    }

    // Error/Info messages produced by gen::GrpcServer
    virtual void OnError(const std::string& err) const override { ERRORMSG(err); }
    virtual void OnInfo(const std::string& info) const override { INFOMSG(info); }
};

int main(int argc, char* argv[])
{
    // Build & start gRpc router server.
    MyRouter srv(FORWARD_HOST, FORWARD_PORT);
    srv.Run(PROXY_PORT, 8 /*number of threads*/);

    INFOMSG("Grpc Proxy Server has stopped");
    return 0;
}


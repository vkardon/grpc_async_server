//
// server.hpp
//
#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include "grpcServer.hpp"
#include "helloService.hpp"
#include "controlService.hpp"
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG
#include "interceptor.hpp"

class MyServer : public gen::GrpcServer
{
public:
    MyServer() = default;
    virtual ~MyServer() = default;

private:
    // gen::GrpcServer overrides
    virtual bool OnInit(::grpc::ServerBuilder& builder) override
    {
        // Add all services
        AddService<HelloService>();
        AddService<ControlService>();

        // Note: Use OnInit for any additional server initialization.
        // For example, to don't allow reusing port:
        builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

        // Example: Set the maximum message size for both inbound and outbound messages
        builder.SetMaxReceiveMessageSize(INT_MAX);
        builder.SetMaxSendMessageSize(INT_MAX);

        // Example: Limit memory and thread usage by the gRPC library
        // ResourceQuota represents a bound on memory and thread usage by the gRPC
        // library. gRPC will attempt to keep memory and threads used by all attached
        // entities below the ResourceQuota bound.
        grpc::ResourceQuota quota;
        quota.Resize(1024 * 1024 * 10); // 10MB memory max
        builder.SetResourceQuota(quota);
        //builder.SetMaxThreads(int new_max_threads);

        // Set how often OnRun() should be called. The default interval is 1 sec,
        // but it can be reset by calling SetRunInterval() with desired time
        // interval in milliseconds.
        // For example, to receive OnRun() every 0.5 seconds:
        SetRunInterval(500);

        // Experimental: Set Interceptor
        std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>> creators;
        creators.push_back(std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>(new MyInterceptorFactory()));
        builder.experimental().SetInterceptorCreators(std::move(creators));

        return true;
    }

    virtual void OnRun() override
    {
        // OnRun is called periodically in the context of the thread that started
        // gRpc server. The default call interval is 1 sec or whatever is set by
        // SetRunInterval(). You can use OnRun for any periodic tasks you might have.
        // Note: OnRun is stopped being called after you call Shutdown.
    }

    virtual void OnError(const std::string& err) const override
    {
        // Error messages produced by gen::GrpcServer
        ERRORMSG(err);
    }

    virtual void OnInfo(const std::string& info) const override
    {
        // Info messages produced by gen::GrpcServer
        INFOMSG(info);
    }
};

#endif // __SERVER_HPP__


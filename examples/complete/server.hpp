//
// server.hpp
//
#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include "grpcServer.hpp"
#include "helloService.hpp"
#include "controlService.hpp"
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG

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

        // Set the maximum message size for both inbound and outbound messages
        builder.SetMaxReceiveMessageSize(INT_MAX);
        builder.SetMaxSendMessageSize(INT_MAX);

        // Set how often OnRun() should be called. The default interval is 1 sec,
        // but it can be reset by calling SetRunInterval() with desired time
        // interval in milliseconds.
        // For example, to receive OnRun() every 0.5 seconds:
        SetRunInterval(500);
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


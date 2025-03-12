//
// helloServiceRouter.hpp
//
#ifndef __HELLO_SERVICE_ROUTER_HPP__
#define __HELLO_SERVICE_ROUTER_HPP__

#include "grpcServer.hpp"
#include "grpcRouter.hpp"    // GrpcRouter
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.
#include "hello.grpc.pb.h"

//
// Note: Derive from gen::GrpcRouterof in order to customize
// initialization and to override OnCallBegin/End, OnError/Info, etc.
//
class HelloServiceRouter : public gen::GrpcRouter<test::Hello>
{
public:
    HelloServiceRouter(const std::string& targetHost, unsigned short targetPort)
    {
        // Example: Set the maximum message size for both inbound and outbound messages
        grpc::ChannelArguments channelArgs;
        channelArgs.SetMaxSendMessageSize(INT_MAX);
        channelArgs.SetMaxReceiveMessageSize(INT_MAX);
//
//        // Example: Limit memory and thread usage by the gRPC library
//        // ResourceQuota represents a bound on memory and thread usage by the gRPC
//        // library. gRPC will attempt to keep memory and threads used by all attached
//        // entities below the ResourceQuota bound.
//        grpc::ResourceQuota quota;
//        quota.Resize(1024 * 1024 * 300); // 300MB memory max
//        channelArgs.SetResourceQuota(quota);

        Init(targetHost, targetPort, nullptr, &channelArgs);

        // Set Async or Sync forwarding method (default is sync)
        SetAsyncForward(true /*asyncForward*/);

        // Set Verbose to get OnInfo() messages
        SetVerbose(true);
    }
    virtual ~HelloServiceRouter() = default;

private:
    virtual ::grpc::Status OnCallBegin(const gen::Context& /*ctx*/, const void** callParam) override
    {
        // This method can be used for authentication and other purposes.
        // If a Status other than OK is returned, the call will be terminated.
        // return { ::grpc::UNAUTHENTICATED, "Invalid session id" };
        //
        // Note: You can use callParam to assign any application-specific value.
        // This value will then be sent to the following methods if you decide to override them:
        // OnCallEnd(callParam)
        // GetMetadata(callParam)
        // FormatError(callParam)
        // FormatInfo(callParam)
        //
        return ::grpc::Status::OK;
    }

    virtual void OnCallEnd(const gen::Context& ctx, const void* callParam) override
    {
        // Note: You can use this method to clean up any resources associated with callParam
    }

    // Error/Info messages produced by gen::GrpcRouter
    virtual void OnError(const char* /*fname*/, int /*lineNum*/, const std::string& err) const override
    {
        ERRORMSG(err);
    }

    virtual void OnInfo(const char* /*fname*/, int /*lineNum*/, const std::string& info) const override
    {
        INFOMSG(info);
    }
};

//
// Router for test::Hello service
//
class HelloService : public gen::GrpcService<test::Hello>
{
public:
    HelloService(const std::string& targetHost, unsigned short targetPort)
        : mRouter(targetHost, targetPort) {}
    virtual ~HelloService() = default;

private:
    // gen::GrpcService overrides
    virtual bool OnInit() override
    {
        // Bind all HelloService RPCs
        Bind(&HelloService::Ping, &test::Hello::AsyncService::RequestPing);
        Bind(&HelloService::ServerStreamTest, &test::Hello::AsyncService::RequestServerStream);
        Bind(&HelloService::ClientStreamTest, &test::Hello::AsyncService::RequestClientStream);
        return true;
    }

    // Supported RPCs
    void Ping(const gen::Context& ctx,
              const test::PingRequest& req, test::PingResponse& resp)
    {
        mRouter.Forward(ctx, req, resp, &test::Hello::Stub::Ping);
    }

    void ServerStreamTest(const gen::ServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp)
    {
        mRouter.Forward(ctx, req, resp, &test::Hello::Stub::ServerStream);
    }

    void ClientStreamTest(const gen::ClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp)
    {
        mRouter.Forward(ctx, req, resp, &test::Hello::Stub::ClientStream);
    }

    // Class to forward requests to test::Hello service
    HelloServiceRouter mRouter;
};

#endif // __HELLO_SERVICE_ROUTER_HPP__


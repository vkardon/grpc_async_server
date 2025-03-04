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
// Note: The only reason to derive from gen::GrpcRouter is
// to override OnError and OnInfo to write to the desired logger
// instead of std::cout and std::cerr
//
class HelloServiceRouter : public gen::GrpcRouter<test::Hello>
{
public:
    HelloServiceRouter(const std::string& targetHost, unsigned short targetPort)
//        : gen::GrpcRouter<test::Hello>(true /*asyncForward*/)
    {
//        // Example: Set the maximum message size for both inbound and outbound messages
//        grpc::ChannelArguments channelArgs;
//        channelArgs.SetMaxSendMessageSize(INT_MAX);
//        channelArgs.SetMaxReceiveMessageSize(INT_MAX);
//
//        // Example: Limit memory and thread usage by the gRPC library
//        // ResourceQuota represents a bound on memory and thread usage by the gRPC
//        // library. gRPC will attempt to keep memory and threads used by all attached
//        // entities below the ResourceQuota bound.
//        grpc::ResourceQuota quota;
//        quota.Resize(1024 * 1024 * 300); // 300MB memory max
//        channelArgs.SetResourceQuota(quota);
//
//        Init(targetHost, targetPort, nullptr, &channelArgs);

        Init(targetHost, targetPort);
    }
    virtual ~HelloServiceRouter() = default;

private:
    // Error/Info messages produced by gen::GrpcRouter
    virtual void OnError(const std::string& /*fname*/, int /*lineNum*/, const std::string& /*func*/,
                         const std::string& err) const override
    {
        ERRORMSG(err);
    }
    virtual void OnInfo(const std::string& /*fname*/, int /*lineNum*/, const std::string& /*func*/,
                        const std::string& info) const override
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


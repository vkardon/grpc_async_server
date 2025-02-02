//
// helloServiceProxy.hpp
//
#ifndef __HELLO_SERVICE_PROXY_HPP__
#define __HELLO_SERVICE_PROXY_HPP__

#include "grpcServer.hpp"
#include "grpcForwarder.hpp"    // GrpcForwarder
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.
#include "hello.grpc.pb.h"

//
// Note: The only reason to derive from gen::GrpcForwarder is
// to override OnError and OnInfo to write to the desired logger
// instead of std::cout and std::cerr
//
class HelloServiceForwarder : public gen::GrpcForwarder<test::Hello>
{
public:
    template <typename... Args>
    HelloServiceForwarder(Args&&... args) : gen::GrpcForwarder<test::Hello>(std::forward<Args>(args)...) {}
    virtual ~HelloServiceForwarder() = default;

private:
    // Error/Info messages produced by gen::GrpcForwarder
    virtual void OnError(const std::string& err) const override { ERRORMSG(err); }
    virtual void OnInfo(const std::string& info) const override { INFOMSG(info); }
};

//
// Proxy for test::Hello service
//
class HelloServiceProxy : public gen::GrpcService<test::Hello>
{
public:
    HelloServiceProxy(const std::string& forwardHost, unsigned short forwardPort)
        : mForwarder(forwardHost, forwardPort) {}
    virtual ~HelloServiceProxy() = default;

private:
    // gen::GrpcService overrides
    virtual bool OnInit() override
    {
        // Bind all HelloService RPCs
        Bind(&HelloServiceProxy::Ping, &test::Hello::AsyncService::RequestPing);
        Bind(&HelloServiceProxy::ServerStreamTest, &test::Hello::AsyncService::RequestServerStream);
        Bind(&HelloServiceProxy::ClientStreamTest, &test::Hello::AsyncService::RequestClientStream);
        return true;
    }

    // Supported RPCs
    void Ping(const gen::RpcContext& ctx,
              const test::PingRequest& req, test::PingResponse& resp)
    {
        mForwarder.Forward(ctx, req, resp, &test::Hello::Stub::Ping);
    }

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp)
    {
        mForwarder.Forward(ctx, req, resp, &test::Hello::Stub::ServerStream);
    }

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp)
    {
        mForwarder.Forward(ctx, req, resp, &test::Hello::Stub::ClientStream);
    }

    // Class to forward requests to test::Hello service
    HelloServiceForwarder mForwarder;
};

#endif // __HELLO_SERVICE_PROXY_HPP__


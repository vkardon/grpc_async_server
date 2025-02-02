//
// helloServiceProxy.hpp
//
#ifndef __HELLO_SERVICE_PROXY_HPP__
#define __HELLO_SERVICE_PROXY_HPP__

#include "grpcServer.hpp"
#include "proxyUtils.hpp"
#include "hello.grpc.pb.h"

class HelloServiceProxy : public gen::GrpcService<test::Hello>
{
public:
    HelloServiceProxy(const std::string& forwardHost, unsigned short forwardPort)
        : mForwardAddressUri(gen::FormatDnsAddressUri(forwardHost, forwardPort)) {}
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
        Forward<test::Hello>(ctx, req, resp, &test::Hello::Stub::Ping, mForwardAddressUri);
    }

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp)
    {
        Forward<test::Hello>(ctx, req, resp, &test::Hello::Stub::ServerStream, mForwardAddressUri);
    }

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp)
    {
        Forward<test::Hello>(ctx, req, resp, &test::Hello::Stub::ClientStream, mForwardAddressUri);
    }

    std::string mForwardAddressUri;
};

#endif // __HELLO_SERVICE_PROXY_HPP__


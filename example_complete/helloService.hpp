//
// helloService.hpp
//
#ifndef __HELLO_SERVICE_HPP__
#define __HELLO_SERVICE_HPP__

#include "grpcServer.hpp"
#include "hello.grpc.pb.h"

class HelloService : public gen::GrpcService<test::Hello>
{
public:
    HelloService() = default;
    virtual ~HelloService() = default;

    // gen::GrpcService overrides
    virtual bool OnInit() override
    {
        // Bind all HelloService RPCs
        // Note: HelloService::RequestPing is the same as test::Hello::AsyncService::RequestPing, etc.
        Bind(&HelloService::PingTest, &HelloService::RequestPing);
        Bind(&HelloService::ServerStreamTest, &HelloService::RequestServerStream);
        Bind(&HelloService::ClientStreamTest, &HelloService::RequestClientStream);
        return true;
    }

protected:
    // Supported RPCs
    void PingTest(const gen::RpcContext& ctx,
                  const test::PingRequest& req, test::PingResponse& resp);

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp);

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp);
};

#endif // __HELLO_SERVICE_HPP__

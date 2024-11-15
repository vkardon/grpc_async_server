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
        Bind(&HelloService::Shutdown,
             &test::Hello::AsyncService::RequestShutdown);
        Bind(&HelloService::PingTest,
             &test::Hello::AsyncService::RequestPing);
        Bind(&HelloService::ServerStreamTest,
             &test::Hello::AsyncService::RequestServerStream);
        Bind(&HelloService::ClientStreamTest,
             &test::Hello::AsyncService::RequestClientStream);
        return true;
    }

    // You can override IsServing for some service-specific check
    // if service is serving.
    //virtual bool IsServing() override
    //{
    //    return true;
    //}

protected:
    // Supported RPCs
    void Shutdown(const gen::RpcContext& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp);

    void PingTest(const gen::RpcContext& ctx,
                  const test::PingRequest& req, test::PingResponse& resp);

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp);

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp);
};

#endif // __HELLO_SERVICE_HPP__


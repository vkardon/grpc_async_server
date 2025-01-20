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
          auto ptr2 = &HelloService::RequestPing;

        // Bind all HelloService RPCs
        Bind(&HelloService::PingTest, &HelloService::RequestPing);
        Bind(&HelloService::ServerStreamTest, &HelloService::RequestServerStream);
        Bind(&HelloService::ClientStreamTest, &HelloService::RequestClientStream);
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
    void PingTest(const gen::RpcContext& ctx,
                  const test::PingRequest& req, test::PingResponse& resp);

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp);

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp);
};

#endif // __HELLO_SERVICE_HPP__


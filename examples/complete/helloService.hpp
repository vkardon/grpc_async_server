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

private:
    // gen::GrpcService overrides
    virtual bool OnInit() override
    {
        // Bind all HelloService RPCs
        Bind(&HelloService::PingTest, &test::Hello::AsyncService::RequestPing);
        Bind(&HelloService::ServerStreamTest, &test::Hello::AsyncService::RequestServerStream);
        Bind(&HelloService::ClientStreamTest, &test::Hello::AsyncService::RequestClientStream);
        return true;
    }

    // Supported RPCs
    void PingTest(const gen::Context& ctx,
                  const test::PingRequest& req, test::PingResponse& resp);

    void ServerStreamTest(const gen::ServerStreamContext& ctx,
                          const test::ServerStreamRequest& req, test::ServerStreamResponse& resp);

    void ClientStreamTest(const gen::ClientStreamContext& ctx,
                          const test::ClientStreamRequest& req, test::ClientStreamResponse& resp);
};

#endif // __HELLO_SERVICE_HPP__

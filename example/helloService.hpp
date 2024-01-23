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
    virtual bool OnInit() override;
    virtual bool IsServing() override;

protected:
    // Supported RPCs
    void Shutdown(const gen::RpcContext& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp);

    void Ping(const gen::RpcContext& ctx,
              const test::PingRequest& req, test::PingResponse& resp);

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamTestRequest& req, test::ServerStreamTestResponse& resp);

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamTestRequest& req, test::ClientStreamTestResponse& resp);
};

#endif // __HELLO_SERVICE_HPP__


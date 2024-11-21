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
        Bind(&HelloService::Ping,
             &test::Hello::AsyncService::RequestPing);
        Bind(&HelloService::Shutdown,
             &test::Hello::AsyncService::RequestShutdown);

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
    void Ping(const gen::RpcContext& ctx,
              const test::PingRequest& req, test::PingResponse& resp);

    void Shutdown(const gen::RpcContext& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp);
};

#endif // __HELLO_SERVICE_HPP__


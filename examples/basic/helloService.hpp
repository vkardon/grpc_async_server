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
        Bind(&HelloService::Ping, &test::Hello::AsyncService::RequestPing);
        return true;
    }

    // Supported RPCs
    void Ping(const gen::Context& ctx,
              const test::PingRequest& req, test::PingResponse& resp)
    {
        std::cout << "From " << ctx.Peer() << std::endl;
        resp.set_msg("Pong");
    }
};

#endif // __HELLO_SERVICE_HPP__


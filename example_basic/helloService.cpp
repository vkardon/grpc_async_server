//
// helloService.cpp
//
#include "helloService.hpp"

void HelloService::Ping(const gen::RpcContext& ctx,
                        const test::PingRequest& req,
                        test::PingResponse& resp)
{
    std::cout << "From " << ctx.Peer() << std::endl;
    resp.set_msg("Pong");
}

void HelloService::Shutdown(const gen::RpcContext& ctx,
                            const test::ShutdownRequest& req,
                            test::ShutdownResponse& resp)
{
    std::cout << "From " << ctx.Peer() << std::endl;
    resp.set_msg("Goodbye");
    srv->Shutdown(); // Shutdown the server
}



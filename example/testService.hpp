#ifndef __TEST_SERVICE_HPP__
#define __TEST_SERVICE_HPP__

#include "grpcServer.hpp"
#include "test.grpc.pb.h"

// Forward declarations
class TestServer;

//
// TestService declaration
//
class TestService : public gen::GrpcService<test::GrpcService>
{
public:
    TestService(TestServer* server) : mServer(server) {}
    virtual ~TestService() = default;

    // gen::GrpcService override
    virtual bool Init() override;
    virtual bool IsServing() override;

protected:
    // RPC methods
    void Shutdown(const gen::RpcContext& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp);

    void Ping(const gen::RpcContext& ctx,
              const test::PingRequest& req, test::PingResponse& resp);

    void ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                          const test::ServerStreamTestRequest& req, test::ServerStreamTestResponse& resp);

    void ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                          const test::ClientStreamTestRequest& req, test::ClientStreamTestResponse& resp);

private:
    TestServer* mServer = nullptr;
};

#endif // __TEST_SERVICE_HPP__


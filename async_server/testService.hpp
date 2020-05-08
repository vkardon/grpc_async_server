#ifndef __TEST_SERVICE_HPP__
#define __TEST_SERVICE_HPP__

#include "grpcContext.hpp"  // gen::GrpcService

//
// Forward declaration of gRpc messages
//
namespace test
{
    class ShutdownRequest;
    class ShutdownResponse;
    class PingRequest;
    class PingResponse;
    class StreamTestRequest;
    class StreamTestResponse;
}

// Forward declarations
class TestServer;

//
// TestService declaration
//
class TestService : public gen::GrpcService
{
public:
    TestService(TestServer* server) : mServer(server) {}
    virtual ~TestService() = default;

    // gen::GrpcService override
    virtual bool Init(gen::GrpcServer* srv);

protected:
    void Shutdown(const gen::RpcContext& ctx,
            const test::ShutdownRequest& req, test::ShutdownResponse& resp);

    void Ping(const gen::RpcContext& ctx,
            const test::PingRequest& req, test::PingResponse& resp);

    void StreamTest(const gen::RpcStreamContext ctx,
            const test::StreamTestRequest& req, test::StreamTestResponse& resp);
private:
    TestServer* mServer = nullptr;
};

#endif // __TEST_SERVICE_HPP__


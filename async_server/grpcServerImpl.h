#ifndef __GRPC_SERVER_IMPL__
#define __GRPC_SERVER_IMPL__

#include <string>
#include <atomic>

//
// Forward declaration of gRpc messages
//
namespace test{ namespace server{ class Result; }}
namespace test{ namespace server{ class ShutdownRequest; }}
namespace test{ namespace server{ class ShutdownResponse; }}
namespace test{ namespace server{ class PingRequest; }}
namespace test{ namespace server{ class PingResponse; }}
namespace test{ namespace server{ class StreamTestRequest; }}
namespace test{ namespace server{ class StreamTestResponse; }}

//
// Grpc Server request/response namespaces
//
using test::server::Result;
using test::server::ShutdownRequest;
using test::server::ShutdownResponse;
using test::server::PingRequest;
using test::server::PingResponse;
using test::server::StreamTestRequest;
using test::server::StreamTestResponse;


// Grpc/protobuf forward declarations
namespace grpc{ class ServerContext; }


//
// GrpcServerImpl declaration
//
class GrpcServerImpl
{
public:
    GrpcServerImpl() = default;
    ~GrpcServerImpl() = default;

    struct Context
    {
        Context(grpc::ServerContext* ctx);
        ~Context() = default;
        void GetMetadata(const char* key, std::string& value);
        grpc::ServerContext* mCtx = nullptr;
        std::string mLoggerPrefix;
    };

    struct StreamContext : public Context
    {
        StreamContext(grpc::ServerContext* ctx) : Context(ctx) {}
        ~StreamContext() = default;
        enum : char { STREAMING=1, SUCCESS, ERROR } mStatus = STREAMING;
        mutable void* mStream = nullptr; // Request-specific stream data
        mutable bool mHasMore = false;   // Are there more responses to stream?
    };

protected:
    // Return true to keep running, false to shutdown
    bool OnRun();

    void Shutdown(const Context& ctx,
            const ShutdownRequest& req, ShutdownResponse& resp);

    void Ping(const Context& ctx,
            const PingRequest& req, PingResponse& resp);

    void StreamTest(const StreamContext& ctx,
            const StreamTestRequest& req, StreamTestResponse& resp);

private:
    // Class data
    std::atomic<bool> mStop{false};
};

#endif // __GRPC_SERVER_IMPL__


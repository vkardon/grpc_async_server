#ifndef __TEST_SERVER_HPP__
#define __TEST_SERVER_HPP__

#include "logger.hpp"
#include "grpcServer.hpp"
#include "testService.hpp"
#include "healthService.hpp"
#include <atomic>

class TestServer : public gen::GrpcServer
{
public:
    TestServer() : testService(this) {}

    virtual ~TestServer() = default;

    // gen::GrpcServer overrides
    virtual bool OnInit() override;
    virtual bool OnRun() override;
    virtual void OnError(const std::string& err) const override;
    virtual void OnInfo(const std::string& info) const override;

    bool Shutdown(const gen::RpcContext& ctx, std::string& errMsg);

private:
    std::atomic<bool> mStop{false};

    TestService testService;
    HealthService healthService;
};

//
// Helper class to set thread-specific logger prefix
//
struct LoggerPrefix
{
    static constexpr const char* METADATA_SESSION_ID = "sessionid";
    static constexpr const char* METADATA_REQUEST_ID = "requestid";

    LoggerPrefix(const gen::RpcContext& ctx)
    {
        // Set thread-specific logging prefix "[session id][request id]"
        std::string sessionId;
        std::string requestId;

        ctx.GetMetadata(METADATA_SESSION_ID, sessionId);
        ctx.GetMetadata(METADATA_REQUEST_ID, requestId);

        char prefix[256] {};
        char* ptr = prefix;
        if(!sessionId.empty())
            ptr += sprintf(ptr, "[sid=%s]", sessionId.c_str());
        if(!requestId.empty())
            ptr += sprintf(ptr, "[rid=%s]", requestId.c_str());
        *ptr = '\0';

        logger::SetThreadPrefix(prefix);
    }

    ~LoggerPrefix()
    {
        logger::SetThreadPrefix("");
    }
};

#endif // __TEST_SERVER_HPP__

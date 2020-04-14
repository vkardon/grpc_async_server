#ifndef __TEST_SERVER_H__
#define __TEST_SERVER_H__

#include "grpcServer.h"    // gen::GrpcServer
#include "testService.h"
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
};

//
// Helper class to set thread-specific logger prefix
//
struct LoggerPrefix
{
    LoggerPrefix(const gen::RpcContext& ctx);
    ~LoggerPrefix();
    std::string mLoggerPrefix;
};

#endif // __TEST_SERVER_H__

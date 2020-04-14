//
// testServer.cpp
//
#include "testServer.h"
#include "logger.h"

//
// Helper LoggerPrefix class to set thread-specific logger prefix
//
const char* METADATA_SESSION_ID = "sessionid";
const char* METADATA_REQUEST_ID = "requestid";

LoggerPrefix::LoggerPrefix(const gen::RpcContext& ctx)
{
    // Set thread-specific logging prefix "[session id][request id]"
    std::string sessionId;
    std::string requestId;

    ctx.GetMetadata(METADATA_SESSION_ID, sessionId);
    ctx.GetMetadata(METADATA_REQUEST_ID, requestId);

    char prefix[256]{};
    char* ptr = prefix;
    if(!sessionId.empty())
        ptr += sprintf(ptr, "[sid=%s]", sessionId.c_str());
    if(!requestId.empty())
        ptr += sprintf(ptr, "[rid=%s]", requestId.c_str());
    *ptr++ = ' ';
    *ptr = '\0';

    mLoggerPrefix = prefix;

    Logger::SetThreadPrefix(mLoggerPrefix.c_str());
}

LoggerPrefix::~LoggerPrefix()
{
    Logger::SetThreadPrefix("");
}

//
// TestServer implementation
//
bool TestServer::OnInit()
{
    return testService.Init(this);
}

bool TestServer::OnRun()
{
    // Return true to keep running, false to shutdown
    return !mStop;
}

void TestServer::OnError(const std::string& err) const
{
    ERRORMSG_MT(err);
}

void TestServer::OnInfo(const std::string& info) const
{
    INFOMSG_MT(info);
}

bool TestServer::Shutdown(const gen::RpcContext& ctx, std::string& errMsg)
{
    // Get client IP addr
    std::string clientAddr = ctx.Peer();

    // Check if this request is from a local host
    // Note: Based on grpc_1.0.0/test/cpp/end2end/end2end_test.cc
    const std::string kIpv6("ipv6:[::1]:");
    const std::string kIpv4MappedIpv6("ipv6:[::ffff:127.0.0.1]:");
    const std::string kIpv4("ipv4:127.0.0.1:");

    bool isLocalHost = (clientAddr.substr(0, kIpv4.size()) == kIpv4 ||
                        clientAddr.substr(0, kIpv4MappedIpv6.size()) == kIpv4MappedIpv6 ||
                        clientAddr.substr(0, kIpv6.size()) == kIpv6);

    if(isLocalHost)
    {
        INFOMSG_MT("From local client " << clientAddr);
        mStop = true; // Force OnRun() to return false
        return true;
    }
    else
    {
        INFOMSG_MT("From remote client " << clientAddr << ": remote shutdown is not allowed");
        errMsg = "Shutdown from remote client is not allowed";
        return false;
    }
}


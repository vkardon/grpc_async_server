//
// testServer.cpp
//
#include "testServer.hpp"
#include "logger.hpp"

//
// TestServer implementation
//
bool TestServer::OnInit(::grpc::ServerBuilder& builder)
{
    // Don't allow reusing port
    // Note: Check other channel arguments here
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

    // Set OnRun idle interval to 1 sec (in milliseconds)
    // Note: this is optional. The default 2 secs interval
    // will be used otherwise, that is fine most of the time.
    SetIdleInterval(1000);

    return (AddService(&testService) && AddService(&healthService));
}

bool TestServer::OnRun()
{
    // Return true to keep running, false to shutdown
    return !mStop;
}

void TestServer::OnError(const std::string& err) const
{
    ERRORMSG(err);
}

void TestServer::OnInfo(const std::string& info) const
{
    INFOMSG(info);
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
        INFOMSG("From local client " << clientAddr);
        mStop = true; // Force OnRun() to return false
        return true;
    }
    else
    {
        INFOMSG("From remote client " << clientAddr << ": remote shutdown is not allowed");
        errMsg = "Shutdown from remote client is not allowed";
        return false;
    }
}


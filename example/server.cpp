//
// server.cpp
//
#include "server.hpp"
#include "logger.hpp"

// MyServer implementation
//
bool MyServer::OnInit(::grpc::ServerBuilder& builder)
{
    // Don't allow reusing port
    // Note: Check other channel arguments here
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

    // Set OnRun idle interval to 1 sec (in milliseconds)
    // Note: this is optional. The default 2 secs interval
    // will be used otherwise, that is fine most of the time.
    SetIdleInterval(1000);

    return (AddService(&helloService) && AddService(&healthService));
}

bool MyServer::OnRun()
{
    // Return true to keep running, false to shutdown
    return !mStop;
}

void MyServer::OnError(const std::string& err) const
{
    ERRORMSG(err);
}

void MyServer::OnInfo(const std::string& info) const
{
    INFOMSG(info);
}

bool MyServer::Shutdown(const gen::RpcContext& ctx, std::string& errMsg)
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

//
// Create and start gRpc Service
//
#include <stdio.h>
#include <libgen.h>     // dirname()
#include "serverConfig.hpp"
#include "grpcUtils.hpp"

int main(int argc, char* argv[])
{
    int threadCount = 8;

    // If server binary name ends with "ssl", then build server SSL/TLS credentials
    std::shared_ptr<grpc::ServerCredentials> creds;
    size_t len = strlen(argv[0]);
    if(len > 3 && !strcmp(argv[0] + len - 3, "ssl"))
    {
        std::string errMsg;
        std::string dir = dirname(argv[0]);

        creds = gen::GetServerCredentials(
                        dir + "/ssl/certs/bundleCA.cert",
                        dir + "/ssl/certs/server.key",
                        dir + "/ssl/certs/server.cert",
                        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY, errMsg);
        if(!creds)
        {
            ERRORMSG(errMsg);
            return 1;
        }
    }

    // Build & start gRpc server
    MyServer srv;

    // Net socket
    srv.Run(PORT_NUMBER, threadCount, creds);

//    // Unix domain socket
//    srv.Run(UNIX_DOMAIN_SOCKET_PATH, threadCount, creds);
//
//    // Unix domain socket in abstract namespace
//    srv.Run(UNIX_DOMAIN_ABSTRACT_SOCKET_PATH, threadCount, creds);

//    // Both Net socket and Unix domain socket
//    std::vector<gen::AddressUri> addressUriArr;
//    addressUriArr.push_back({ gen::FormatDnsAddressUri("0.0.0.0", PORT_NUMBER), creds });
//    addressUriArr.push_back({ gen::FormatUnixDomainSocketAddressUri(UNIX_DOMAIN_SOCKET_PATH) });
//    srv.Run(addressUriArr, threadCount);

    INFOMSG("Grpc Server has stopped");
    return 0;
}


//
// server.cpp
//
#include "server.hpp"
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG

// MyServer implementation
//
bool MyServer::OnInit(::grpc::ServerBuilder& builder)
{
    // Don't allow reusing port
    // Note: Check other channel arguments here
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

    // Set OnRun idle interval to 0.5 sec (in milliseconds)
    // Note: this is optional. The default 1 sec interval
    // will be used otherwise, that is fine most of the time.
    SetIdleInterval(500);
    return true;
}

void MyServer::OnRun()
{
    // OnRun is called on periodically with the interval set
    // in OnInit() by SetIdleInterval(). You can use it for
    // anything you want.
}

void MyServer::OnError(const std::string& err) const
{
    ERRORMSG(err);
}

void MyServer::OnInfo(const std::string& info) const
{
    INFOMSG(info);
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


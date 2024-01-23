//
// server.cpp
//
#include "server.hpp"
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG

bool MyServer::OnInit(::grpc::ServerBuilder& builder)
{
    // Note: Use OnInit for any additional server initialization.
    // For example, to don't allow reusing port:
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

    // Set how often OnRun() should be called. The default interval is 1 sec,
    // but it can be reset by calling SetRunInterval() with desired time
    // interval in milliseconds.
    // For example, to receive OnRun() every 0.5 seconds:
    SetRunInterval(500);
    return true;
}

void MyServer::OnRun()
{
    // OnRun is called periodically in the context of the thread that started
    // gRpc server. The default call interval is 1 sec or whatever is set by
    // SetRunInterval(). Use OnRun for any periodic tasks you might have.
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

    // Build & start gRpc server.
    // Note: The simple gRpc server uses basic initialization and writes
    // info and error messages into std::cout and stc::cerr respectively.
    // gen::SimpleGrpcServer srv;
    // srv.AddService<HelloService>();
    // srv.AddService<HealthService>();
    // srv.Run(PORT_NUMBER, threadCount, creds);

    // If we need any customization, like using desired loggers for
    // info and error messages, using OnRun() calls, etc., then we need
    // to have our own service class that is derived from gen::GrpcServer.
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


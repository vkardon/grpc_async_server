//
// main.cpp
//
#include <stdio.h>
#include <libgen.h>     // dirname()
#include "testServer.hpp"
#include "testServerConfig.hpp"
#include "logger.hpp"
#include "grpcUtils.hpp"

int main(int argc, char *argv[])
{
    int threadCount = 8;

    // If server binary name ends with "ssl", then build server SSL/TSL credentials
    std::shared_ptr<grpc::ServerCredentials> creds;
    size_t len = strlen(argv[0]);
    if(len > 3 && !strcmp(argv[0] + len - 3, "ssl"))
    {
        std::string errMsg;
        std::string dir = dirname(argv[0]);
        creds = gen::GetServerCredentials(
                dir + "/ssl/ca-cert.pem",
                dir + "/ssl/server-key.pem",
                dir + "/ssl/server-cert.pem",
                errMsg);
        if(!creds)
        {
            ERRORMSG(errMsg);
            return 1;
        }
    }

    // Build & start gRpc server
    TestServer srv;

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


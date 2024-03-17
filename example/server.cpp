//
// server.cpp
//
#include <stdio.h>
#include <libgen.h>          // dirname()
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG
#include "server.hpp"

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
    MyServer srv;

    // Listen on Net socket
    srv.Run(PORT_NUMBER, threadCount, creds);

//    // Listen on Unix Domain socket
//    std::string addressUri = gen::FormatDomainSocketAddressUri(UNIX_DOMAIN_SOCKET_PATH);
//    srv.Run(addressUri, threadCount, creds);

//    // Listen on Unix Domain socket in abstract namespace
//    std::string addressUri = gen::FormatAbstractSocketAddressUri(UNIX_ABSTRACT_SOCKET_PATH);
//    srv.Run(addressUri, threadCount, creds);

//    // Listen on both Net socket and Unix Domain socket
//    std::vector<gen::AddressUri> addressUriArr;
//    addressUriArr.push_back({ gen::FormatDnsAddressUri("0.0.0.0", PORT_NUMBER), creds });
//    addressUriArr.push_back({ gen::FormatDomainSocketAddressUri(UNIX_DOMAIN_SOCKET_PATH) });
//    srv.Run(addressUriArr, threadCount);

    INFOMSG("Grpc Server has stopped");
    return 0;
}


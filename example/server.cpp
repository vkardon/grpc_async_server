//
// main.cpp
//
#include <stdio.h>
#include "testServer.hpp"
#include "testServerConfig.hpp"
#include "logger.hpp"
#include "grpcUtils.hpp"

int main(int argc, char *argv[])
{
    int threadCount = 8;

    // Build & start gRpc server
    TestServer srv;

    if(!strcmp(URI, "domain_socket"))
    {
        // Unix domain socket
        srv.Run(UNIX_DOMAIN_SOCKET_PATH, threadCount);
    }
    else if(!strcmp(URI, "domain_abstract_socket"))
    {
        // Unix domain socket in abstract namespace
        srv.Run(UNIX_DOMAIN_ABSTRACT_SOCKET_PATH, threadCount);
    }
    else //(!strcmp(URI, "dns"))
    {
        // Net socket
        srv.Run(PORT_NUMBER, threadCount);

        // Both Net socket and Unix domain socket
        // std::vector<gen::AddressUri> addressUriArr;
        // addressUriArr.push_back({ gen::FormatDnsAddressUri("0.0.0.0", PORT_NUMBER) });
        // addressUriArr.push_back({ gen::FormatUnixDomainSocketAddressUri(UNIX_DOMAIN_SOCKET_PATH) });
        // srv.Run(addressUriArr, threadCount);
    }

    INFOMSG("Grpc Server has stopped");
    return 0;
}


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
        char socket_path[256]{};
        socket_path[0] = '\0';
        strcpy(socket_path + 1, UNIX_DOMAIN_ABSTRACT_SOCKET_PATH);
        srv.Run(socket_path, threadCount);
    }
//    else if(!strcmp(URI, "dns_and_domain_socket"))
//    {
//        // User both Net socket and Unix domain socket
//        std::vector<std::string> addressUriArr;
//        addressUriArr.push_back(gen::FormatDnsAddressUri("0.0.0.0", PORT_NUMBER));
//        addressUriArr.push_back(gen::FormatUnixDomainSocketAddressUri(UNIX_DOMAIN_SOCKET_PATH));
//        srv.Run(addressUriArr, threadCount);
//    }
    else //(!strcmp(URI, "dns"))
    {
        // Net socket
        srv.Run(PORT_NUMBER, threadCount);
    }

    INFOMSG_MT("Grpc Server has stopped");
    return 0;
}


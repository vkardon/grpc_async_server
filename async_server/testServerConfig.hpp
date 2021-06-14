#ifndef __TEST_SERVER_CONFIG_HPP__
#define __TEST_SERVER_CONFIG_HPP__

#define PORT_NUMBER                         50051
#define UNIX_DOMAIN_SOCKET_PATH             "/tmp/grpc_server_test.sock"
#define UNIX_DOMAIN_ABSTRACT_SOCKET_PATH    "grpc_server_test.sock"

#define URI "dns"
//#define URI "domain_socket"

// Abstract socket is linux-specific
#ifdef __linux__
#define URI "domain_abstract_socket"
#endif __linux

#endif // __TEST_SERVER_CONFIG_HPP__

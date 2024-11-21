//
// client.cpp
//
#include <grpcpp/grpcpp.h>
#include "serverConfig.hpp"  // for PORT_NUMBER
#include "hello.grpc.pb.h"

bool PingTest(const char* addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
    grpc::ClientContext context;

    test::PingRequest req;
    test::PingResponse resp;

    grpc::Status s = stub->Ping(&context, req, &resp);

    if(!s.ok())
    {
        std::cerr << "PintTest failed with error code: " 
            << s.error_code() << " (" << s.error_message() << ")" << std::endl;
        return false;
    }

    std::cout << "PingTest response: " << resp.msg() << std::endl;
    return true;
}

bool ShutdownTest(const char* addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
    grpc::ClientContext context;

    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    req.set_reason("Shutdown Test");
    grpc::Status s = stub->Shutdown(&context, req, &resp);

    if(!s.ok())
    {
        std::cerr << "ShutdownTest failed with error code: " 
            << s.error_code() << " (" << s.error_message() << ")" << std::endl;
        return false;
    }

    std::cout << "ShutdownTest response: " << resp.msg() << std::endl;
    return true;
}

void PrintUsage(const char* arg = nullptr)
{
    if(arg)
        printf("Unwknown test name '%s'\n", arg);

    printf("Usage: client <hostname (optional)> <test name>\n");
    printf("       client ping\n");
    printf("       client shutdown\n");
}

int main(int argc, char** argv)
{
    // Read hostname if we have it
    const char* host = (argc > 2 ? argv[1] : "localhost");

    // Format Net socket address URI
    char addressUri[256]{};
    sprintf(addressUri, "%s:%d", host, PORT_NUMBER);

    // Get the name of the test
    const char* testName = (argc > 2 ? argv[2] : argc > 1 ? argv[1] : nullptr);
    if(!testName)
    {
        PrintUsage();
        return 0;
    }

    // Call gRpc service
    if(!strcmp(testName, "ping"))
    {
        PingTest(addressUri);
    }
    else if(!strcmp(testName, "shutdown"))
    {
        ShutdownTest(addressUri);
    }
    else
    {
        PrintUsage(testName);
    }

    return 0;
}


//
// client.cpp
//
#include <grpcpp/grpcpp.h>
#include "serverConfig.hpp"  // for PORT_NUMBER
#include "hello.grpc.pb.h"

void PingTest(const char* addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
    grpc::ClientContext context;

    test::PingRequest req;
    test::PingResponse resp;

    if(grpc::Status s = stub->Ping(&context, req, &resp); !s.ok())
    {
        std::cerr << "PintTest failed with error code: " 
            << s.error_code() << " (" << s.error_message() << ")" << std::endl;
    }
    else
    {
        std::cout << "PingTest response: " << resp.msg() << std::endl;
    }
}

void ShutdownTest(const char* addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
    grpc::ClientContext context;

    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    req.set_reason("Shutdown Test");

    if(grpc::Status s = stub->Shutdown(&context, req, &resp); !s.ok())
    {
        std::cerr << "ShutdownTest failed with error code: " 
            << s.error_code() << " (" << s.error_message() << ")" << std::endl;
    }
    else
    {
        std::cout << "ShutdownTest response: " << resp.msg() << std::endl;
    }
}

void PrintUsage()
{
    std::cout << "Usage: client <hostname (optional)> <test name>" << std::endl;
    std::cout << "       client ping" << std::endl;
    std::cout << "       client shutdown" << std::endl;
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
        std::cerr << "Unwknown test name '" << testName << "'" << std::endl;
        PrintUsage();
    }

    return 0;
}


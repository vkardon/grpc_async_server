//
// client.cpp
//
#include <grpcpp/grpcpp.h>
#include <grpc/fork.h>
#include <sys/wait.h>
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

void ForkTest(const char* addressUri)
{
    // Note: grpc_prefork() requires gRPC to be initialized prior to its invocation.
    // Calling grpc_prefork() before gRPC is initialized will result in a crash.
    // It appears that even though grpc_prefork() might internally attempt to check
    // for gRPC initialization, this check may not function reliably. Therefore, the
    // workaround is to explicitly call grpc_is_initialized() before the first grpc_prefork()
    // call. While grpc_is_initialized() does not itself initialize gRPC, calling it
    // preceding grpc_prefork() prevents the crash that occurs when grpc_prefork() is
    // called before gRPC is initialized.
    int res = grpc_is_initialized();
    std::cout << "grpc_is_initialized() returned " << res << std::endl;

    int num_children = 0;
    for(; num_children < 50; num_children++)
    {
        // Parent call gRpc service
        PingTest(addressUri);
        PingTest(addressUri);
        PingTest(addressUri);

        std::cout << "Parent before grpc_prefork" << std::endl;
        grpc_prefork();
        std::cout << "Parent after grpc_prefork" << std::endl;

        pid_t pid = fork();
        if(pid > 0)
        {
            // Parent process
            std::cout << "Parent before grpc_postfork_parent" << std::endl;
            grpc_postfork_parent();
            std::cout << "Parent after grpc_postfork_parent" << std::endl;
        }
        else if(pid == 0)
        {
            // Child process
            std::cout << "Child before grpc_postfork_child" << std::endl;
            grpc_postfork_child();
            std::cout << "Child after grpc_postfork_child" << std::endl;

            // Child calls gRpc service
            for(int i=0; i<500; i++)
            {
                std::cout << "PingTest #" << (i + 1) << ":" << std::endl;
                PingTest(addressUri);
            }
            return; // Child exits
        }
        else
        {
            std::cout << "Fork failed" << std::endl;
            return;
        }
    }

    // Parent waits for all children
    for (int i = 0; i < num_children; ++i)
        wait(nullptr); // Wait for any child

    std::cout << "All children finished" << std::endl;
}

void PrintUsage()
{
    std::cout << "Usage: client <hostname (optional)> <test name>" << std::endl;
    std::cout << "       client ping" << std::endl;
    std::cout << "       client fork" << std::endl;
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
    else if(!strcmp(testName, "fork"))
    {
        ForkTest(addressUri);
    }
    else
    {
        std::cerr << "Unwknown test name '" << testName << "'" << std::endl;
        PrintUsage();
    }

    return 0;
}


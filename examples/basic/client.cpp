//
// client.cpp
//
#include <grpcpp/grpcpp.h>
#include <grpc/fork.h>
#include <sys/wait.h>
#include <grpc/support/port_platform.h>
#include "serverConfig.hpp"  // for PORT_NUMBER
#include "hello.grpc.pb.h"

void PingTest(const char* addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created

    // Channel-based compression
    // grpc::ChannelArguments channel_args;
    // channel_args.SetCompressionAlgorithm(grpc_compression_algorithm::GRPC_COMPRESS_GZIP);
    // std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(addressUri, grpc::InsecureChannelCredentials(), channel_args);
    // // std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    // std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
    // grpc::ClientContext context;

    // Per-call compression
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
    grpc::ClientContext context;
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);

    test::PingRequest req;
    test::PingResponse resp;

    if(grpc::Status s = stub->Ping(&context, req, &resp); !s.ok())
    {
        std::cerr << "PintTest failed with error code: " << s.error_code() << " (" << s.error_message() << ")" << std::endl;
    }
    else
    {
        std::cout << "PingTest response: " << resp.msg() << std::endl;
    }
}

// Process Parent/Child/GrandChild status used by ForkTest
int gRole{1}; // Initially, 1-->PARENT, 2-->CHILD, 3-->GRANDCHILD
std::string GetRole()
{
    return std::to_string(getpid()) +
        (gRole == 1 ? " (PARENT)     : " : 
         gRole == 2 ? " (CHILD)      : " : 
         gRole == 3 ? " (GRANDCHILD) : " : 
                      " (UNKNOWN)    : ");
};
#define TRACE(msg) std::cout << GetRole() << msg << std::endl

void ForkTest(const char* addressUri)
{
    auto Ping = [addressUri]() 
    {
        std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
        std::unique_ptr<test::Hello::Stub> stub = test::Hello::NewStub(channel);
        grpc::ClientContext context;
        test::PingRequest req;
        test::PingResponse resp;
        if(grpc::Status s = stub->Ping(&context, req, &resp); !s.ok())
            TRACE("PintTest failed with error code: " << s.error_code() << " (" << s.error_message() << ")");
    };

    gRole = 1; // PARENT
    int childrenCount = 5;
    int grandChildrenCount = 30;
    int rpcCount = 100;

    // Parent started
    TRACE("Running...");

    // Parent calls gRpc service
    for(int j = 0; j < rpcCount; j++)
        Ping();

    // Parent forks children
    setpgid(0, getpid()); // Make parent a process group leader
    for(int n = 0; n < childrenCount; n++)
    {
        grpc_prefork();

        pid_t pid = fork();
        if(pid > 0)
        {
            // Parent process
            grpc_postfork_parent();
        }
        else if(pid == 0)
        {
            // Child process
            grpc_postfork_child();

            gRole++; // Transition PARENT-->CHILD
            TRACE("Running...");

            setpgid(0, getppid()); // Join parent's process group
            break;
        }
        else
        {
            // If fork fails, kill the entire process group
            TRACE("Fork failed");
            kill(-getpgrp(), SIGTERM);
            exit(1);
        }
    }

    // If we are parent, then wait for children then exit
    if(gRole == 1) // Parent process
    {
        for(int i = 0; i < childrenCount; ++i)
            wait(nullptr); // Wait for any child

        TRACE("All children finished (" << childrenCount << ")");
        return;
    }

    // Child process continue; calls gRpc service
    for(int j = 0; j < rpcCount; j++)
        Ping();

    // Child forks grandchildren
    pid_t pgid = getpgrp(); // Child process group id
    for(int i = 0; i < grandChildrenCount; i++)
    {
        grpc_prefork();

        pid_t pid = fork();
        if(pid > 0)
        {
            // Parent process
            grpc_postfork_parent();
        }
        else if(pid == 0)
        {
            // Grandchild process
            grpc_postfork_child();

            gRole++; // Transition CHILD-->GRANDCHILD
            setpgid(0, pgid); // Join grandparent's process group

            // TRACE("Running...");

            // Grandchild calls gRpc service
            for(int j = 0; j < rpcCount; j++)
                Ping();
            
            return; // Grandchild exits
        }
        else
        {
            // If fork fails, kill the entire process group
            TRACE("Fork failed");
            kill(-getpgrp(), SIGTERM);
            return;
        }
    }

    // Child  - waits for grandchildren, then exit
    for(int i = 0; i < grandChildrenCount; ++i)
        wait(nullptr); // Wait for any grandchild

    TRACE("All grandchildren finished (" << grandChildrenCount << ")");
}

void PrintUsage()
{
    std::cout << getpid() << ": Usage: client <hostname (optional)> <test name>" << std::endl;
    std::cout << getpid() << ":        client ping" << std::endl;
    std::cout << getpid() << ":        client fork" << std::endl;
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


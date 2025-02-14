//
// client.cpp
//
#include <libgen.h>     // dirname()
#include <string>
#include <thread>
#include "grpcClient.hpp"
#include "serverConfig.hpp"     // PORT_NUMBER, etc.
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.
#include "hello.grpc.pb.h"
#include "control.grpc.pb.h"

// Channel SSL/TLS credentials
std::shared_ptr<grpc::ChannelCredentials> gCreds;

bool PingTest(const std::string& addressUri)
{
    test::PingRequest req;
    test::PingResponse resp;

    // Set optional metadata "key-value" data if you need/want any.
    // If you don't need any metadata, then use no-metadata version of Call().
    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);
    unsigned long timeout = 1000; // milliseconds

    std::string errMsg;
    gen::GrpcClient<test::Hello> grpcClient(addressUri, gCreds);

    if(!grpcClient.Call(&test::Hello::Stub::Ping, req, resp, metadata, errMsg, timeout))
    {
        ERRORMSG(errMsg);
        return false;
    }

    INFOMSG(resp);
    return true;
}

bool ServerStreamTest(const std::string& addressUri, bool silent = false)
{
    test::ServerStreamRequest req;
    req.set_msg("ServerStreamRequest");

    std::list<test::ServerStreamResponse> respList;
    std::function respCallback = [&respList](const test::ServerStreamResponse& resp) -> bool
    {
        // std::cout << resp.msg() << std::endl;
        respList.push_back(resp);
        return true;
    };

    std::string errMsg;
    gen::GrpcClient<test::Hello> grpcClient(addressUri, gCreds);
    if(!grpcClient.CallStream(&test::Hello::Stub::ServerStream, req, respCallback, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    if(!silent)
    {
        // Dump all collected responses
        std::unique_lock<std::mutex> lock(logger::sLogMutex);
        std::cout << "BEGIN" << std::endl;
        for(const test::ServerStreamResponse& resp : respList)
            std::cout << resp << std::endl;
        std::cout << "END: " << respList.size() << " responses" << std::endl;
    }

    return true;
}

bool ClientStreamTest(const std::string& addressUri)
{
    int count = 0;
    std::function reqCallback = [&count](test::ClientStreamRequest& req) -> bool
    {
        if(++count > 20)
            return false;   // Return false to stop streaming
        INFOMSG("Client-side streaming message " + std::to_string(count));
        req.set_msg("ClientStreamRequest " + std::to_string(count));
        return true;
    };
 
    test::ClientStreamResponse resp;

    std::string errMsg;
    gen::GrpcClient<test::Hello> grpcClient(addressUri, gCreds);
    if(!grpcClient.CallClientStream(&test::Hello::Stub::ClientStream, reqCallback, resp, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    INFOMSG(resp);
    return true;
}

bool ShutdownTest(const std::string& addressUri)
{
    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    req.set_reason("Shutdown Test");

    std::string errMsg;
    gen::GrpcClient<test::Control> grpcClient(addressUri, gCreds);
    if(!grpcClient.Call(&test::Control::Stub::Shutdown, req, resp, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    INFOMSG(resp);
    return true;
}

bool StatusTest(const std::string& addressUri, const std::string& serviceName)
{
    test::StatusRequest req;
    test::StatusResponse resp;

    req.set_service_name(serviceName);

    std::string errMsg;
    gen::GrpcClient<test::Control> grpcClient(addressUri, gCreds);
    if(!grpcClient.Call(&test::Control::Stub::Status, req, resp, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    INFOMSG(resp);
    return true;
}

void LoadTest(const std::string& addressUri)
{
    const int numClientThreads = 100;  // Number of threads
    const int numRpcs = 50;            // Number of RPCs per thread

    // Start threads
    INFOMSG("Sending requests using " << numClientThreads
            << " threads with " << numRpcs << " RPC requests per thread");

    StopWatch duration(("Duration [" + std::to_string(numClientThreads * numRpcs) + " calls]: ").c_str());

    std::vector<std::thread> threads(numClientThreads);

    for(std::thread& thread : threads)
    {
        thread = std::thread([&]()
        {
            for(int i = 0; i < numRpcs; ++i)
            {
                // Format request message and call RPC
                if(!ServerStreamTest(addressUri, true /*silent*/))
                    break;
            }
        });
    }

    // Wait for all threads to complete
    for(std::thread& thread : threads)
    {
        thread.join();
    }

    INFOMSG("All client threads are completed");
}

void PrintUsage()
{
    std::cout << "Usage: client <host:port (optional)> <test name>" << std::endl;
    std::cout << "       client localhost:50055 ping" << std::endl;
    std::cout << "       client ping" << std::endl;
    std::cout << "       client serverstream" << std::endl;
    std::cout << "       client clientstream" << std::endl;
    std::cout << "       client shutdown" << std::endl;
    std::cout << "       client status" << std::endl;
    std::cout << "       client load" << std::endl;
}

int main(int argc, char** argv)
{
    // Read hostname and port number if provided
    std::string addressUri;
    if(argc > 2)
        addressUri = argv[1];
    else
        addressUri = "localhost:" + std::to_string(PORT_NUMBER);

    // Get the name of the test
    const char* testName = (argc > 2 ? argv[2] : argc > 1 ? argv[1] : nullptr);
    if(!testName)
    {
        PrintUsage();
        return 0;
    }

    // If client binary name ends with "ssl", then build client SSL/TLS credentials
    size_t len = strlen(argv[0]);
    if(len > 3 && !strcmp(argv[0] + len - 3, "ssl"))
    {
        std::string errMsg;
        std::string dir = dirname(argv[0]);

        gCreds = gen::GetChannelCredentials(
                     dir + "/ssl/certs/bundleCA.cert",
                     dir + "/ssl/certs/client.key",
                     dir + "/ssl/certs/client.cert",
                     errMsg);
        if(!gCreds)
        {
            ERRORMSG(errMsg);
            return 1;
        }
    }

    // Call gRpc service
    if(!strcmp(testName, "ping"))
    {
        PingTest(addressUri);
    }
    else if(!strcmp(testName, "serverstream"))
    {
        ServerStreamTest(addressUri);
    }
    else if(!strcmp(testName, "clientstream"))
    {
        ClientStreamTest(addressUri);
    }
    else if(!strcmp(testName, "shutdown"))
    {
        ShutdownTest(addressUri);
    }
    else if(!strcmp(testName, "status"))
    {
        // Check the status of the given service
        StatusTest(addressUri, "");                     // Ask for overall status
        StatusTest(addressUri, "test.Hello");           // Ask for test.Hello service status
        StatusTest(addressUri, "test.Control");         // Ask for test.Control service status
        StatusTest(addressUri, "test.DummyService");    // Ask for not existing service
    }
    else if(!strcmp(testName, "load"))
    {
        LoadTest(addressUri);
    }
    else
    {
        std::cout << "Unwknown test name '" << testName << "'" << std::endl;
        PrintUsage();
    }

    return 0;
}


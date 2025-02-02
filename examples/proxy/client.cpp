//
// client.cpp
//
#include "grpcClient.hpp"
#include "serverConfig.hpp"     // for PROXY_HOST, PROXY_PORT, etc.
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.
#include "hello.grpc.pb.h"
#include "control.grpc.pb.h"

void PingTest(const char* addressUri)
{
    test::PingRequest req;
    test::PingResponse resp;

    // Set optional metadata "key-value" data if you need/want any.
    // If you don't need any metadata, then use no-metadata version of Call().
    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::Hello> grpcClient(addressUri);

    if(!grpcClient.Call(&test::Hello::Stub::Ping, req, resp, metadata, errMsg))
    {
        std::cerr << errMsg << std::endl;
    }
    else
    {
        std::cout << "PingTest response: " << resp << std::endl;
    }
}

void ServerStreamTest(const char* addressUri, bool silent = false)
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
    gen::GrpcClient<test::Hello> grpcClient(addressUri);
    if(!grpcClient.CallStream(&test::Hello::Stub::ServerStream, req, respCallback, errMsg))
    {
        std::cerr << errMsg << std::endl;
    }
    else if(!silent)
    {
        // Dump all collected responses
        std::unique_lock<std::mutex> lock(logger::sLogMutex);
        std::cout << "BEGIN" << std::endl;
        for(const test::ServerStreamResponse& resp : respList)
            std::cout << resp << std::endl;
        std::cout << "END: " << respList.size() << " responses" << std::endl;
    }
}

//bool ClientStreamTest()
//{
//    int count = 0;
//    std::function reqCallback = [&count](test::ClientStreamRequest& req) -> bool
//    {
//        if(++count > 20)
//            return false;   // Return false to stop streaming
//        INFOMSG("Client-side streaming message " + std::to_string(count));
//        req.set_msg("ClientStreamRequest " + std::to_string(count));
//        return true;
//    };
//
//    test::ClientStreamResponse resp;
//
//    std::string errMsg;
//    gen::GrpcClient<test::Hello> grpcClient(gHost, PORT_NUMBER, gCreds);
//    if(!grpcClient.CallClientStream(&test::Hello::Stub::ClientStream, reqCallback, resp, errMsg))
//    {
//        ERRORMSG(errMsg);
//        return false;
//    }
//
//    INFOMSG(resp);
//    return true;
//}

bool ShutdownTest(const char* addressUri)
{
    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    req.set_reason("Shutdown Test");

    std::string errMsg;
    gen::GrpcClient<test::Control> grpcClient(addressUri);
    if(!grpcClient.Call(&test::Control::Stub::Shutdown, req, resp, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    INFOMSG(resp);
    return true;
}

//void LoadTest()
//{
//    const int numClientThreads = 100;  // Number of threads
//    const int numRpcs = 50;            // Number of RPCs per thread
//
//    // Start threads
//    INFOMSG("Sending requests using " << numClientThreads
//            << " threads with " << numRpcs << " RPC requests per thread");
//
//    StopWatch duration(("Duration [" + std::to_string(numClientThreads * numRpcs) + " calls]: ").c_str());
//
//    std::vector<std::thread> threads(numClientThreads);
//
//    for(std::thread& thread : threads)
//    {
//        thread = std::thread([&]()
//        {
//            for(int i = 0; i < numRpcs; ++i)
//            {
//                // Format request message and call RPC
//                if(!ServerStreamTest(true /*silent*/))
//                    break;
//            }
//        });
//    }
//
//    // Wait for all threads to complete
//    for(std::thread& thread : threads)
//    {
//        thread.join();
//    }
//
//    INFOMSG("All client threads are completed");
//}

void PrintUsage()
{
    std::cout << "Usage: client <hostname (optional)> <test name>" << std::endl;
    std::cout << "       client ping" << std::endl;
    std::cout << "       client serverstream" << std::endl;
//    std::cout << "       client clientstream" << std::endl;
    std::cout << "       client shutdown" << std::endl;
//    std::cout << "       client load" << std::endl;
}

int main(int argc, char** argv)
{
    // Read hostname if we have it
    const char* host = (argc > 2 ? argv[1] : PROXY_HOST);

    // Format Net socket address URI
    char addressUri[256]{};
    sprintf(addressUri, "%s:%d", host, PROXY_PORT);

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
    else if(!strcmp(testName, "serverstream"))
    {
        ServerStreamTest(addressUri);
    }
//    else if(!strcmp(testName, "clientstream"))
//    {
//        ClientStreamTest(addressUri);
//    }
    else if(!strcmp(testName, "shutdown"))
    {
        ShutdownTest(addressUri);
    }
//    else if(!strcmp(testName, "load"))
//    {
//        LoadTest(addressUri);
//    }
    else
    {
        std::cerr << "Unwknown test name '" << testName << "'" << std::endl;
        PrintUsage();
    }


    return 0;
}


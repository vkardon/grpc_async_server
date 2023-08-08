//
// client
//
#include <libgen.h>     // dirname()
#include <string>
#include <thread>
#include "grpcClient.hpp"
#include "testServerConfig.hpp"
#include "test.grpc.pb.h"
#include "health.grpc.pb.h"
#include "logger.hpp"

// Channel SSL/TLS credentials
std::shared_ptr<grpc::ChannelCredentials> gCreds;
std::string gHost = "localhost";

bool PingTest()
{
    test::PingRequest req;
    test::PingResponse resp;

    // Note: metadata is optional. There is no-metadata version of Call() as well.
    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient(gHost, PORT_NUMBER, gCreds);
    if(!grpcClient.Call(&test::GrpcService::Stub::Ping, req, resp, metadata, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG(result);
    return true;
}

bool ServerStreamTest(bool silent = false)
{
    test::ServerStreamTestRequest req;
    req.set_msg("ServerStreamTestRequest");

    struct ResponseCallback : public gen::RespCallbackFunctor<test::ServerStreamTestResponse>
    {
        bool operator()(const test::ServerStreamTestResponse& resp) override
        {
//            std::cout << resp.msg() << std::endl;
            respList.push_back(resp);
            return true;
        }

        void Dump()
        {
            // Dump all collected responses
            std::unique_lock<std::mutex> lock(logger::sLogMutex);
            std::cout << "BEGIN" << std::endl;
            for(const test::ServerStreamTestResponse& resp : respList)
            {
                std::cout << resp.msg() << std::endl;
            }
            std::cout << "END: " << respList.size() << " responses" << std::endl;
        }

        std::list<test::ServerStreamTestResponse> respList;

    } respCallback;

    // Note: metadata is optional. There is no-metadata version of Call() as well.
    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient(gHost, PORT_NUMBER, gCreds);
    if(!grpcClient.CallStream(&test::GrpcService::Stub::ServerStreamTest, req, respCallback, metadata, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    if(!silent)
        respCallback.Dump();

    return true;
}

bool ClientStreamTest()
{
    struct RequestCallback : public gen::ReqCallbackFunctor<test::ClientStreamTestRequest>
    {
        bool operator()(test::ClientStreamTestRequest& req) override
        {
            if(++count > 20)
                return false;
            INFOMSG("Client-side streaming message " + std::to_string(count));
            req.set_msg("ClientStreamTestRequest " + std::to_string(count));
            return true;
        }

        int count{0};
    } reqCallback;

    test::ClientStreamTestResponse resp;

    // Note: metadata is optional. There is no-metadata version of Call() as well.
    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient(gHost, PORT_NUMBER, gCreds);
    if(!grpcClient.CallClientStream(&test::GrpcService::Stub::ClientStreamTest, reqCallback, resp, metadata, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG(result);
    return true;
}

bool ShutdownTest()
{
    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    // Note: metadata is optional. There is no-metadata version of Call() as well.
    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    req.set_reason("Shutdown Test");

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient(gHost, PORT_NUMBER, gCreds);
    if(!grpcClient.Call(&test::GrpcService::Stub::Shutdown, req, resp, metadata, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    const char* result = (resp.result() ? "success" : resp.msg().c_str());
    INFOMSG(result);
    return true;
}

bool HealthTest(const std::string& serviceName)
{
    grpc::health::v1::HealthCheckRequest req;
    grpc::health::v1::HealthCheckResponse resp;

    req.set_service(serviceName);

    std::string errMsg;
    gen::GrpcClient<grpc::health::v1::Health> grpcClient(gHost, PORT_NUMBER, gCreds);
    if(!grpcClient.Call(&grpc::health::v1::Health::Stub::Check, req, resp, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    grpc::health::v1::HealthCheckResponse::ServingStatus status = resp.status();

    const char* result =
        (status == grpc::health::v1::HealthCheckResponse::UNKNOWN         ? "UNKWONW" :
         status == grpc::health::v1::HealthCheckResponse::SERVING         ? "SERVING" :
         status == grpc::health::v1::HealthCheckResponse::NOT_SERVING     ? "NOT_SERVING" :
         status == grpc::health::v1::HealthCheckResponse::SERVICE_UNKNOWN ? "SERVICE_UNKNOWN" : "INVALID");

    if(serviceName.empty())
        INFOMSG("Server status: " << result);
    else
        INFOMSG("Service '" << serviceName << "' status: " << result);

    return true;
}

void LoadTest()
{
    const int numClientThreads = 100;  // Number of threads
    const int numRpcs = 50;            // Number of RPCs per thread

    // Start threads
    INFOMSG("Sending requests using " << numClientThreads
            << " threads with " << numRpcs << " RPC requests per thread");

    CTimeElapsed elapsed(("Elapsed time [" + std::to_string(numClientThreads * numRpcs) + " calls]: ").c_str());

    std::vector<std::thread> threads(numClientThreads);

    for(std::thread& thread : threads)
    {
        thread = std::thread([&]()
        {
            for(int i = 0; i < numRpcs; ++i)
            {
                // Format request message and call RPC
                if(!ServerStreamTest(true /*silent*/))
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

void PrintUsage(const char* arg = nullptr)
{
    if(arg)
        printf("Unwknown test name '%s'\n", arg);

    printf("Usage: client <ssl:hostname (optional)> <test name>\n");
    printf("       client ping\n");
    printf("       client serverstream\n");
    printf("       client clientstream\n");
    printf("       client shutdown\n");
    printf("       client health\n");
    printf("       client load\n");
}

int main(int argc, char** argv)
{
    // Read hostname if we have it
    if(argc > 2)
        gHost = argv[1];

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
        PingTest();
    }
    else if(!strcmp(testName, "serverstream"))
    {
        ServerStreamTest();
    }
    else if(!strcmp(testName, "clientstream"))
    {
        ClientStreamTest();
    }
    else if(!strcmp(testName, "shutdown"))
    {
        ShutdownTest();
    }
    else if(!strcmp(testName, "health"))
    {
        // Check overall server status and status of each service
        HealthTest("");                      // Ask for overall status
        HealthTest("test.GrpcService");      // Ask for test.GrpcService service status
        HealthTest("grpc.health.v1.Health"); // Ask for grpc.health.v1.Health service status
        HealthTest("test.DummyService");     // Ask for not existing service
    }
    else if(!strcmp(testName, "load"))
    {
        LoadTest();
    }
    else
    {
        PrintUsage(testName);
    }

    return 0;
}


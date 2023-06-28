//
// client
//
#include <string>
#include <thread>
#include "grpcClient.hpp"
#include "testServerConfig.hpp"
#include "test.grpc.pb.h"
#include "health.grpc.pb.h"
#include "logger.hpp"

bool PingTest()
{
    test::PingRequest req;
    test::PingResponse resp;

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient("localhost", PORT_NUMBER);
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

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient("localhost", PORT_NUMBER);
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

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient("localhost", PORT_NUMBER);
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

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    req.set_reason("Shutdown Test");

    std::string errMsg;
    gen::GrpcClient<test::GrpcService> grpcClient("localhost", PORT_NUMBER);
    if(!grpcClient.Call(&test::GrpcService::Stub::Shutdown, req, resp, metadata, errMsg))
    {
        ERRORMSG(errMsg);
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG(result);
    return true;
}

bool HealthTest(const std::string& serviceName)
{
    grpc::health::v1::HealthCheckRequest req;
    grpc::health::v1::HealthCheckResponse resp;

    req.set_service(serviceName);

    std::string errMsg;
    gen::GrpcClient<grpc::health::v1::Health> grpcClient("localhost", PORT_NUMBER);
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

    INFOMSG(result);
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

    printf("Usage: client <test name>\n");
    printf("       client ping\n");
    printf("       client serverstream\n");
    printf("       client clientstream\n");
    printf("       client shutdown\n");
    printf("       client health\n");
    printf("       client load\n");
}

int main(int argc, char** argv)
{
    const char* cmd = (argc < 2 ? nullptr : argv[1]);
    if(!cmd)
    {
        PrintUsage();
        return 0;
    }

    // Call gRpc service
    if(!strcmp(cmd, "ping"))
    {
        PingTest();
    }
    else if(!strcmp(cmd, "serverstream"))
    {
        ServerStreamTest();
    }
    else if(!strcmp(cmd, "clientstream"))
    {
        ClientStreamTest();
    }
    else if(!strcmp(cmd, "shutdown"))
    {
        ShutdownTest();
    }
    else if(!strcmp(cmd, "health"))
    {
        HealthTest(argc > 2 ? argv[2]: "");
    }
    else if(!strcmp(cmd, "load"))
    {
        LoadTest();
    }
    else
    {
        PrintUsage(cmd);
    }

    return 0;
}


//
// client
//
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>     // sleep()
#include <sys/time.h>   // gettimeofday()
#include <sstream>      // std::stringstream
#include <thread>
#include "grpcClient.hpp"
#include "testServerConfig.hpp"

#include "test.grpc.pb.h"
#include "health.grpc.pb.h"
#include "logger.hpp"

//
// Helper CTimeElapsed class to measure elapsed time
//
class CTimeElapsed
{
    struct timeval start_tv;
    struct timeval stop_tv;
    std::string prefix;

public:
    CTimeElapsed(const char* _prefix="") : prefix(_prefix) { gettimeofday(&start_tv, NULL); }
    ~CTimeElapsed()
    {
        gettimeofday(&stop_tv, NULL);
        //long long elapsed = (stop_tv.tv_sec - start_tv.tv_sec)*1000000 + (stop_tv.tv_usec - start_tv.tv_usec);
        //printf("%lld microseconds\n", elapsed);

        timeval tmp;
        if(stop_tv.tv_usec < start_tv.tv_usec)
        {
            tmp.tv_sec = stop_tv.tv_sec - start_tv.tv_sec - 1;
            tmp.tv_usec = 1000000 + stop_tv.tv_usec - start_tv.tv_usec;
        }
        else
        {
            tmp.tv_sec = stop_tv.tv_sec - start_tv.tv_sec;
            tmp.tv_usec = stop_tv.tv_usec - start_tv.tv_usec;
        }

        printf("%s%ld.%06lu sec\n", prefix.c_str(), tmp.tv_sec, (long)tmp.tv_usec);
    }
};

bool PingTest(gen::GrpcClient& grpcClient)
{
    test::PingRequest req;
    test::PingResponse resp;

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    if(!grpcClient.Call<test::GrpcService>(&test::GrpcService::Stub::Ping, req, resp, metadata))
    {
        ERRORMSG_MT(grpcClient.GetError());
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG_MT(result);
    return true;
}

bool ServerStreamTest(gen::GrpcClient& grpcClient, bool silent = false)
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
            std::unique_lock<std::mutex> lock(logger::GetMutex());
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

    if(!grpcClient.CallStream<test::GrpcService>(&test::GrpcService::Stub::ServerStreamTest, req, respCallback, metadata))
    {
        ERRORMSG_MT(grpcClient.GetError());
        return false;
    }

    if(!silent)
        respCallback.Dump();

    return true;
}

bool ClientStreamTest(gen::GrpcClient& grpcClient)
{
    struct RequestCallback : public gen::ReqCallbackFunctor<test::ClientStreamTestRequest>
    {
        bool operator()(test::ClientStreamTestRequest& req) override
        {
            if(++count > 20)
                return false;
            INFOMSG_MT("Client-side streaming message " + std::to_string(count));
            req.set_msg("ClientStreamTestRequest " + std::to_string(count));
            return true;
        }

        int count{0};
    } reqCallback;

    test::ClientStreamTestResponse resp;

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    if(!grpcClient.CallClientStream<test::GrpcService>(&test::GrpcService::Stub::ClientStreamTest, reqCallback, resp, metadata))
    {
        ERRORMSG_MT(grpcClient.GetError());
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG_MT(result);
    return true;
}

bool ShutdownTest(gen::GrpcClient& grpcClient)
{
    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    std::map<std::string, std::string> metadata;
    metadata["sessionid"] = std::to_string(rand() % 1000);
    metadata["requestid"] = std::to_string(rand() % 1000);

    req.set_reason("Shutdown Test");

    if(!grpcClient.Call<test::GrpcService>(&test::GrpcService::Stub::Shutdown, req, resp, metadata))
    {
        ERRORMSG_MT(grpcClient.GetError());
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG_MT(result);
    return true;
}

bool HealthTest(gen::GrpcClient& grpcClient, const char* serviceName)
{
    grpc::health::v1::HealthCheckRequest req;
    grpc::health::v1::HealthCheckResponse resp;

    req.set_service(serviceName ? serviceName : "");

    if(!grpcClient.Call<grpc::health::v1::Health>(&grpc::health::v1::Health::Stub::Check, req, resp))
    {
        ERRORMSG_MT(grpcClient.GetError());
        return false;
    }

    grpc::health::v1::HealthCheckResponse::ServingStatus status = resp.status();

    const char* result =
        (status == grpc::health::v1::HealthCheckResponse::UNKNOWN         ? "UNKWONW" :
         status == grpc::health::v1::HealthCheckResponse::SERVING         ? "SERVING" :
         status == grpc::health::v1::HealthCheckResponse::NOT_SERVING     ? "NOT_SERVING" :
         status == grpc::health::v1::HealthCheckResponse::SERVICE_UNKNOWN ? "SERVICE_UNKNOWN" : "INVALID");

    INFOMSG_MT(result);
    return true;
}

void LoadTest(gen::GrpcClient& grpcClient)
{
    const int numClientThreads = 100;  // Number of threads
    const int numRpcs = 50;            // Number of RPCs per thread

    // Start threads
    INFOMSG_MT("Sending requests using " << numClientThreads
               << " threads with " << numRpcs << " RPC requests per thread");

    CTimeElapsed elapsed(("Elapsed time [" + std::to_string(numClientThreads * numRpcs) + " calls]: ").c_str());

    std::vector<std::thread> threads(numClientThreads);

    for(std::thread& thread : threads)
    {
        thread = std::thread([&]()
        {
            gen::GrpcClient thisGrpcClient;
            thisGrpcClient.InitFromAddressUri(grpcClient.GetAddressUri());

            for(int i = 0; i < numRpcs; ++i)
            {
                // Format request message and call RPC
                if(!ServerStreamTest(thisGrpcClient, true /*silent*/))
                    break;
            }
        });
    }

    // Wait for all threads to complete
    for(std::thread& thread : threads)
    {
        thread.join();
    }

    INFOMSG_MT("All client threads are completed");
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
    if(argc < 2)
    {
        PrintUsage();
        return 0;
    }

    // Initialize gRpc client
    gen::GrpcClient grpcClient;

    if(!strcmp(URI, "domain_socket"))
    {
        // Unix domain socket
        grpcClient.Init(UNIX_DOMAIN_SOCKET_PATH);
    }
    else if(!strcmp(URI, "domain_abstract_socket"))
    {
        // Unix domain socket in abstract namespace
        // Note: the first character in socket path must be '\0'.
        char buf[256]{0};
        strcpy(buf+1, UNIX_DOMAIN_ABSTRACT_SOCKET_PATH);
        grpcClient.Init(buf);
    }
    else //(!strcmp(URI, "dns"))
    {
        // Net socket
        grpcClient.Init("localhost", PORT_NUMBER);
    }

    // Call gRpc service
    if(!strcmp(argv[1], "ping"))
    {
        PingTest(grpcClient);
    }
    else if(!strcmp(argv[1], "serverstream"))
    {
        ServerStreamTest(grpcClient);
    }
    else if(!strcmp(argv[1], "clientstream"))
    {
        ClientStreamTest(grpcClient);
    }
    else if(!strcmp(argv[1], "shutdown"))
    {
        ShutdownTest(grpcClient);
    }
    else if(!strcmp(argv[1], "health"))
    {
        HealthTest(grpcClient, (argc > 2 ? argv[2]: nullptr));
    }
    else if(!strcmp(argv[1], "load"))
    {
        LoadTest(grpcClient);
    }
    else
    {
        PrintUsage(argv[1]);
    }

    return 0;
}


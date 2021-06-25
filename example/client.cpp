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
#include "testServerConfig.hpp"

#include <grpcpp/grpcpp.h>

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

//
// grpc::StatusCode to string conversion routine
//
const char* StatusToStr(grpc::StatusCode code)
{
    switch(code)
    {
    case grpc::StatusCode::OK:                  return "OK";
    case grpc::StatusCode::CANCELLED:           return "CANCELLED";
    case grpc::StatusCode::UNKNOWN:             return "UNKNOWN";
    case grpc::StatusCode::INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
    case grpc::StatusCode::DEADLINE_EXCEEDED:   return "DEADLINE_EXCEEDED";
    case grpc::StatusCode::NOT_FOUND:           return "NOT_FOUND";
    case grpc::StatusCode::ALREADY_EXISTS:      return "ALREADY_EXISTS";
    case grpc::StatusCode::PERMISSION_DENIED:   return "PERMISSION_DENIED";
    case grpc::StatusCode::UNAUTHENTICATED:     return "UNAUTHENTICATED";
    case grpc::StatusCode::RESOURCE_EXHAUSTED:  return "RESOURCE_EXHAUSTED";
    case grpc::StatusCode::FAILED_PRECONDITION: return "FAILED_PRECONDITION";
    case grpc::StatusCode::ABORTED:             return "ABORTED";
    case grpc::StatusCode::OUT_OF_RANGE:        return "OUT_OF_RANGE";
    case grpc::StatusCode::UNIMPLEMENTED:       return "UNIMPLEMENTED";
    case grpc::StatusCode::INTERNAL:            return "INTERNAL";
    case grpc::StatusCode::UNAVAILABLE:         return "UNAVAILABLE";
    case grpc::StatusCode::DATA_LOSS:           return "DATA_LOSS";
    case grpc::StatusCode::DO_NOT_USE:          return "DO_NOT_USE";
    default:                                    return "INVALID_ERROR_CODE";
    }
}

bool PingTest(const std::string& addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::GrpcService::Stub> stub = test::GrpcService::NewStub(channel);

    // Set random sessionId and requestId
    grpc::ClientContext context;
    context.AddMetadata("sessionid", std::to_string(rand() % 1000));
    context.AddMetadata("requestid", std::to_string(rand() % 1000));

    test::PingRequest req;
    test::PingResponse resp;

    grpc::Status s = stub->Ping(&context, req, &resp);

    if(!s.ok())
    {
        std::string err = s.error_message();
        if(err.empty())
            err = StatusToStr(s.error_code());

        ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG_MT(result);
    return true;
}

bool ServerStreamTest(const std::string& addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::GrpcService::Stub> stub = test::GrpcService::NewStub(channel);

    test::ServerStreamTestRequest req;
    req.set_msg("ServerStreamTestRequest");

    // Set random sessionId and requestId
    grpc::ClientContext context;
    context.AddMetadata("sessionid", std::to_string(rand() % 1000));
    context.AddMetadata("requestid", std::to_string(rand() % 1000));

    // Send request and read responses
    std::list<test::ServerStreamTestResponse> respList;

    test::ServerStreamTestResponse resp;
    std::unique_ptr<grpc::ClientReader<test::ServerStreamTestResponse> > reader(stub->ServerStreamTest(&context, req));
    while(reader->Read(&resp))
    {
        //INFOMSG_MT("Got Record: " << resp.msg());
        respList.push_back(resp);
    }
    grpc::Status s = reader->Finish();

    if(!s.ok())
    {
        std::string err = s.error_message();
        if(err.empty())
            err = StatusToStr(s.error_code());

        ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
        return false;
    }

    // Dump all collected responses
    std::unique_lock<std::mutex> lock(logger::GetMutex());
    std::cout << "BEGIN" << std::endl;
    for(const test::ServerStreamTestResponse& resp : respList)
    {
        std::cout << resp.msg() << std::endl;
    }
    std::cout << "END: " << respList.size() << " responses" << std::endl;

    return true;
}

bool ClientStreamTest(const std::string& addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::GrpcService::Stub> stub = test::GrpcService::NewStub(channel);

    // Set random sessionId and requestId
    grpc::ClientContext context;
    context.AddMetadata("sessionid", std::to_string(rand() % 1000));
    context.AddMetadata("requestid", std::to_string(rand() % 1000));

    test::ClientStreamTestResponse resp;
    std::unique_ptr<grpc::ClientWriter<test::ClientStreamTestRequest>> writer(stub->ClientStreamTest(&context, &resp));

    for(int i = 0; i < 20; i++)
    {
        INFOMSG_MT("Client-side streaming message " + std::to_string(i + 1));

        test::ClientStreamTestRequest req;
        req.set_msg("ClientStreamTestRequest " + std::to_string(i + 1));

        if(!writer->Write(req))
        {
            // Broken stream
            ERRORMSG_MT("Client-side streaming failed");
            return false;
        }

        //usleep(100000);
    }
    writer->WritesDone();
    grpc::Status s = writer->Finish();

    if(!s.ok())
    {
        std::string err = s.error_message();
        if(err.empty())
            err = StatusToStr(s.error_code());

        ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
        return false;
    }

    const char* result = (resp.result() ? "success" : "error");
    INFOMSG_MT(result);
    return true;
}

bool ShutdownTest(const std::string& addressUri)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<test::GrpcService::Stub> stub = test::GrpcService::NewStub(channel);

    // Set random sessionId and requestId
    grpc::ClientContext context;
    context.AddMetadata("sessionid", std::to_string(rand() % 1000));
    context.AddMetadata("requestid", std::to_string(rand() % 1000));

    test::ShutdownRequest req;
    test::ShutdownResponse resp;

    req.set_reason("Shutdown Test");
    grpc::Status s = stub->Shutdown(&context, req, &resp);

    if(!s.ok())
    {
        std::string err = s.error_message();
        if(err.empty())
            err = StatusToStr(s.error_code());

        ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
        return false;
    }

    const char* result = (resp.result() ? "success" : resp.msg().c_str());
    INFOMSG_MT(result);
    return true;
}

bool HealthTest(const std::string& addressUri, const char* serviceName)
{
    // Instantiate a channel, out of which the actual RPCs are created
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<grpc::health::v1::Health::Stub> stub = grpc::health::v1::Health::NewStub(channel);

    // Set random sessionId and requestId
    grpc::ClientContext context;

    grpc::health::v1::HealthCheckRequest req;
    grpc::health::v1::HealthCheckResponse resp;

    req.set_service(serviceName ? serviceName : "");

    grpc::Status s = stub->Check(&context, req, &resp);

    if(!s.ok())
    {
        std::string err = s.error_message();
        if(err.empty())
            err = StatusToStr(s.error_code());

        ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
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

void LoadTest(const std::string& addressUri)
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
        // Capture threadIndex by value, capture other variables by reference
        thread = std::thread([&]()
        {
            for(int i = 0; i < numRpcs; ++i)
            {
                // Format request message and call RPC
                if(!ServerStreamTest(addressUri))
                    break;

//                // Client-streaming test
//                if(!client.ClientStreamTest(addressUri))
//                    break;
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
    char addressUri[256] {};

    if(!strcmp(URI, "domain_socket"))
    {
        // Unix domain socket
        sprintf(addressUri, "unix://%s", UNIX_DOMAIN_SOCKET_PATH);
    }
    else if(!strcmp(URI, "domain_abstract_socket"))
    {
        // Unix domain socket in abstract namespace
        sprintf(addressUri, "unix-abstract:%s", UNIX_DOMAIN_ABSTRACT_SOCKET_PATH);
    }
    else //(!strcmp(URI, "dns"))
    {
        // Net socket
        sprintf(addressUri, "localhost:%d", PORT_NUMBER);
    }

    // To call the server, we need to instantiate a channel, out of which
    // the actual RPCs are created. This channel models a connection to an
    // endpoint specified by addressUri. We are going to indicate that the
    // channel isn't authenticated (use of InsecureChannelCredentials()).
    // Note: We are going to instantiate channel in every RPCs simply to
    // demonstrate necessary steps. In real application, channel should
    // be created once per addressUri and shared across RPCs.

    if(argc > 1)
    {
        if(!strcmp(argv[1], "ping"))
        {
            PingTest(addressUri);
        }
        else if(!strcmp(argv[1], "serverstream"))
        {
            ServerStreamTest(addressUri);
        }
        else if(!strcmp(argv[1], "clientstream"))
        {
            ClientStreamTest(addressUri);
        }
        else if(!strcmp(argv[1], "shutdown"))
        {
            ShutdownTest(addressUri);
        }
        else if(!strcmp(argv[1], "health"))
        {
            HealthTest(addressUri, (argc > 2 ? argv[2]: nullptr));
        }
        else if(!strcmp(argv[1], "load"))
        {
            LoadTest(addressUri);
        }
        else
        {
            PrintUsage(argv[1]);
        }
    }
    else
    {
        PrintUsage();
    }

    return 0;
}


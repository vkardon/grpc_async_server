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

#include <grpc++/grpc++.h>

#include "test.grpc.pb.h"
#include "logger.h"

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
    default:                                    return "Unknown Error Code";
    }
}

//
// Grpc Client implementation
// 
class GrpcClient
{
public:
    GrpcClient(std::shared_ptr<grpc::Channel> channel)
    {
        stub_ = test::GrpcService::NewStub(channel);
    }

    bool StreamTest(const std::string& reqMsg)
    {
        test::StreamTestRequest req;
        req.set_msg(reqMsg);

        // Set random sessionId and requestId
        grpc::ClientContext context;
        context.AddMetadata("sessionid", std::to_string(rand() % 1000));
        context.AddMetadata("requestid", std::to_string(rand() % 1000));

        // Send request and read responses
        std::list<test::StreamTestResponse> respList;

        test::StreamTestResponse resp;
        std::unique_ptr<grpc::ClientReader<test::StreamTestResponse> > reader(stub_->StreamTest(&context, req));
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
        std::unique_lock<std::mutex> lock(Logger::GetMutex());
        std::cout << "BEGIN" << std::endl;
        for(const test::StreamTestResponse& resp : respList)
        {
            std::cout << resp.msg() << std::endl;
        }
        std::cout << "END: " << respList.size() << " responses" << std::endl;

        return true;
    }

    bool Ping()
    {
        // Set random sessionId and requestId
        grpc::ClientContext context;
        context.AddMetadata("sessionid", std::to_string(rand() % 1000));
        context.AddMetadata("requestid", std::to_string(rand() % 1000));

        test::PingRequest req;
        test::PingResponse resp;

        grpc::Status s = stub_->Ping(&context, req, &resp);

        if(!s.ok())
        {
            std::string err = s.error_message();
            if(err.empty())
                err = StatusToStr(s.error_code());

            ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
            return false;
        }

        const char* result = (resp.result().status() == test::Result::SUCCESS ? "success" : resp.result().message().c_str());
        INFOMSG_MT(result);
        return true;
    }

    bool Shutdown()
    {
        // Set random sessionId and requestId
        grpc::ClientContext context;
        context.AddMetadata("sessionid", std::to_string(rand() % 1000));
        context.AddMetadata("requestid", std::to_string(rand() % 1000));

        test::ShutdownRequest req;
        test::ShutdownResponse resp;

        req.set_reason("Shutdown Test");
        grpc::Status s = stub_->Shutdown(&context, req, &resp);

        if(!s.ok())
        {
            std::string err = s.error_message();
            if(err.empty())
                err = StatusToStr(s.error_code());

            ERRORMSG_MT("Failed with error " << s.error_code() << ": " << err);
            return false;
        }

        const char* result = (resp.result().status() == test::Result::SUCCESS ? "success" : resp.result().message().c_str());
        INFOMSG_MT(result);
        return true;
     }

private:
    std::unique_ptr<test::GrpcService::Stub> stub_;
};

int main(int argc, char** argv)
{
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint (in this case,
    // localhost at port 50051). We indicate that the channel isn't authenticated
    // (use of InsecureChannelCredentials()).
    GrpcClient client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));

    if(argc > 1 && !strcmp(argv[1], "ping"))
    {
       client.Ping();
       return 0;
    }
    else if(argc > 1 && !strcmp(argv[1], "shutdown"))
    {
       client.Shutdown();
       return 0;
    }

    // If not arguments sent, then run stream test by default
    const int numClientThreads = 100;  // Number of threads
    const int numRpcs = 50;            // Number of RPCs per thread

//    const int numClientThreads = 1;  // Number of threads
//    const int numRpcs = 1;           // Number of RPCs per thread

    // Start threads
    INFOMSG_MT("Sending requests using " << numClientThreads 
              << " threads with " << numRpcs << " RPC requests per thread");

    CTimeElapsed elapsed(("Elapsed time [" + std::to_string(numClientThreads * numRpcs) + " calls]: ").c_str());

    std::vector<std::thread> threads(numClientThreads);

    int threadIndex = 0;
    for(std::thread& thread : threads)
    {
        // Capture threadIndex by value, capture other variables by reference
        thread = std::thread([&, threadIndex]()
            {
                char reqMsg[64]{};
                for(int i = 0; i < numRpcs; ++i)
                {
                    // Format request message and call RPC
                    sprintf(reqMsg, "Req: %d, Thread Indx: %d", i+1, threadIndex);
                    if(!client.StreamTest(reqMsg))
                        break;
                }
            });
        threadIndex++;
    }

    // Wait for all threads to complete
    for(std::thread& thread : threads)
    {
        thread.join();
    }

    INFOMSG_MT("All client threads are completed");
    return 0;
}


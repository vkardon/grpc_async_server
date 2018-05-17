//
// grpcServer.cpp
//
// Note: The multithreaded gRpc server is implemented Based on
//       grpc_1.0.0/test/cpp/end2end/thread_stress_test.cc
//

#include "grpcServer.h"
#include "grpcServerImpl.h"
#include "logger.h"

#include <thread>
#include <mutex>
#include <signal.h>   // pthread_sigmask
#include <unistd.h>   // sleep

#include <grpc++/grpc++.h>
#include "grpcServer.grpc.pb.h"

// gRpc namespaces
using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerReaderWriter;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerAsyncWriter;
using grpc::CompletionQueue;

// Our Grpc Service namespace
using test::server::GrpcService;


//const int GRPC_MAX_MESSAGE_SIZE = 1024 * 8; // 8K

//
// Helper macros PROCESS_UNARY and PROCESS_STREAM to start processing requests.
//
// The equivalent code example is below:
//
//    std::vector<RequestContext<ShutdownRequest, ShutdownResponse> > shutdown_contexts(context_count);
//    for(RequestContext<ShutdownRequest, ShutdownResponse>& ctx : shutdown_contexts)
//    {
//        ctx.fProcessPtr = &GrpcServer::Shutdown;
//        ctx.fRequestPtr = &GrpcService::AsyncService::RequestShutdown;
//        ctx.StartProcessing(this);
//    }

//
// Create array of contexts and start processing unary RPC request.
//
#define PROCESS_UNARY(REQ, RESP, RPC, IMPL, CONTEXT_COUNT)                       \
    std::vector<RequestContext<REQ, RESP> > RPC##_contexts(CONTEXT_COUNT);       \
    for(RequestContext<REQ, RESP>& ctx : RPC##_contexts)                         \
    {                                                                            \
        ctx.fProcessPtr = &GrpcServer::IMPL;                                     \
        ctx.fRequestPtr = &GrpcService::AsyncService::Request##RPC;              \
        ctx.StartProcessing(this);                                               \
    }

//
// Create array of contexts and start processing streaming RPC request.
//
#define PROCESS_STREAM(REQ, RESP, RPC, IMPL, CONTEXT_COUNT)                      \
    std::vector<RequestStreamContext<REQ, RESP> > RPC##_contexts(CONTEXT_COUNT); \
    for(RequestStreamContext<REQ, RESP>& ctx : RPC##_contexts)                   \
    {                                                                            \
        ctx.fProcessPtr = &GrpcServer::IMPL;                                     \
        ctx.fRequestPtr = &GrpcService::AsyncService::Request##RPC;              \
        ctx.StartProcessing(this);                                               \
    }

//
// Class GrpcServer
//
class GrpcServer : public GrpcServerImpl
{
public:
    GrpcServer(const char* serverAddress, int threadCount)
    : serverAddress_(serverAddress), threadCount_(threadCount) { /**/ }

    virtual ~GrpcServer() = default;

    // Run() is blocked. It doesn't return until Stop() is called from another thread.
    void Run();

private:
    struct RequestContextBase
    {
        RequestContextBase() = default;
        ~RequestContextBase() = default;

        std::unique_ptr<ServerContext> srv_ctx;
        enum : char { UNKNOWN=0, REQUEST, WRITE, FINISH } state = UNKNOWN;
        virtual void StartProcessing(GrpcServer* srv) = 0;
        virtual void Process(GrpcServer* srv) = 0;
        virtual void EndProcessing(GrpcServer* srv, bool isError) = 0;
    };

    //
    // Template class to handle unary respone
    //
    template<class Request, class Response>
    struct RequestContext : public RequestContextBase
    {
        RequestContext() : fProcessPtr(nullptr), fRequestPtr(nullptr) {};
        ~RequestContext() = default;

        Request req;
        std::unique_ptr<ServerAsyncResponseWriter<Response>> resp_writer;

        // Pointer to function that does actual processing
        void (GrpcServer::*fProcessPtr)(const GrpcServerImpl::Context&, const Request&, Response&);

        // Pointer to function that *request* the system to start processing given requests
        void (GrpcService::AsyncService::*fRequestPtr)(ServerContext*, Request*,
                ServerAsyncResponseWriter<Response>*, CompletionQueue*, ServerCompletionQueue*, void*);

        void StartProcessing(GrpcServer* srv)
        {
            state = RequestContextBase::REQUEST;
            srv_ctx.reset(new ServerContext);
            resp_writer.reset(new ServerAsyncResponseWriter<Response>(srv_ctx.get()));
            req.Clear();

            // *Request* that the system start processing given requests.
            // In this request, "this" acts as the tag uniquely identifying
            // the request (so that different context instances can serve
            // different requests concurrently), in this case the memory address
            // of this context instance.
            (srv->service_.*fRequestPtr)(srv_ctx.get(), &req, resp_writer.get(),
                    srv->cq_.get(), srv->cq_.get(), this);
        }

        void Process(GrpcServer* srv)
        {
            // The actual processing
            Response resp;
            (srv->*fProcessPtr)(GrpcServerImpl::Context(srv_ctx.get()), req, resp);

            // And we are done! Let the gRPC runtime know we've finished, using the
            // memory address of this instance as the uniquely identifying tag for
            // the event.
            state = RequestContextBase::FINISH;
            resp_writer->Finish(resp, Status::OK, this);
        }

        void EndProcessing(GrpcServer* srv, bool /*isError*/)
        {
            // TODO
            // Handle processing errors ...

            // Ask the system start processing requests
            StartProcessing(srv);
        }
    };

    //
    // Template class to handle streaming respone
    //
    template<class Request, class Response>
    struct RequestStreamContext : public RequestContextBase
    {
        RequestStreamContext() : fProcessPtr(nullptr), fRequestPtr(nullptr) {};
        ~RequestStreamContext() = default;

        Request req;
        std::unique_ptr<ServerAsyncWriter<Response>> resp_writer;
        std::unique_ptr<GrpcServerImpl::StreamContext> stream_ctx;

        // Pointer to function that does actual processing
        void (GrpcServer::*fProcessPtr)(const GrpcServerImpl::StreamContext&, const Request&, Response&);

        // Pointer to function that *request* the system to start processing given requests
        void (GrpcService::AsyncService::*fRequestPtr)(ServerContext*, Request*,
                ServerAsyncWriter<Response>*, CompletionQueue*, ServerCompletionQueue*, void*);

        void StartProcessing(GrpcServer* srv)
        {
            state = RequestContextBase::REQUEST;
            srv_ctx.reset(new ServerContext);
            resp_writer.reset(new ServerAsyncWriter<Response>(srv_ctx.get()));
            stream_ctx.reset();
            req.Clear();

//            OUTMSG_MT("Calling fRequestPtr(), tag='" << this << "', "
//                    << "state=" << (state == RequestContextBase::REQUEST ? "REQUEST" :
//                                    state == RequestContextBase::WRITE   ? "WRITE"   :
//                                    state == RequestContextBase::FINISH  ? "FINISH"  : "UNKNOWN"));

            // *Request* that the system start processing given requests.
            // In this request, "this" acts as the tag uniquely identifying
            // the request (so that different context instances can serve
            // different requests concurrently), in this case the memory address
            // of this context instance.
            (srv->service_.*fRequestPtr)(srv_ctx.get(), &req, resp_writer.get(),
                    srv->cq_.get(), srv->cq_.get(), this);
        }

        void Process(GrpcServer* srv)
        {
            if(state == RequestContextBase::REQUEST)
            {
                // This is very first Process call for the given request.
                state = RequestContextBase::WRITE;

                // Create new GrpcServerImpl::StreamContext
                stream_ctx.reset(new GrpcServerImpl::StreamContext(srv_ctx.get()));
            }

            // The actual processing
            Response resp;
            (srv->*fProcessPtr)(*stream_ctx, req, resp);

            // Are there more responses to stream?
            if(stream_ctx->mHasMore)
            {
//                OUTMSG_MT("Calling Write(), tag='" << this << "', "
//                        << "state=" << (state == RequestContextBase::REQUEST ? "REQUEST" :
//                                        state == RequestContextBase::WRITE   ? "WRITE"   :
//                                        state == RequestContextBase::FINISH  ? "FINISH"  : "UNKNOWN"));

                resp_writer->Write(resp, this);
            }
            // There are no more responses to stream
            else
            {
                // And we are done! Let the gRPC runtime know we've finished, using the
                // memory address of this instance as the uniquely identifying tag for
                // the event.
                state = RequestContextBase::FINISH;

//                OUTMSG_MT("Calling Finish(), tag='" << this << "', "
//                        << "state=" << (state == RequestContextBase::REQUEST ? "REQUEST" :
//                                        state == RequestContextBase::WRITE   ? "WRITE"   :
//                                        state == RequestContextBase::FINISH  ? "FINISH"  : "UNKNOWN"));

                resp_writer->Finish(Status::OK, this);
            }
        }

        void EndProcessing(GrpcServer* srv, bool isError)
        {
            if(stream_ctx)
            {
                if(isError)
                {
                    ERRORMSG_MT("Error streaming for tag '" << this << "', stream='" << stream_ctx->mStream << "', "
                        << "state=" <<
                        (state == RequestContextBase::REQUEST ? "REQUEST (ServerAsyncWriter::fRequestPtr() failed)" :
                         state == RequestContextBase::WRITE   ? "WRITE (ServerAsyncWriter::Write() failed)"   :
                         state == RequestContextBase::FINISH  ? "FINISH (ServerAsyncWriter::Finish() failed)" : "UNKNOWN"));
                }

                // End processing
                stream_ctx->mStatus = (isError ? StreamContext::ERROR : StreamContext::SUCCESS);
                Response resp;
                (srv->*fProcessPtr)(*stream_ctx, req, resp);
            }
            else
            {
                // Note: stream_ctx is set by a very first Process() call, that is called
                // after successful compleation of the event placed by fRequestPtr().
                // If processing of fRequestPtr() event failed, then we will be here
                // even before stream_ctx gets a chance to be initialized.
                // In this case we don't have a stream yet.

                ERRORMSG_MT("Ending streaming for tag '" << this << "', "
                        << "stream='Not Started', "
                        << "state=" <<
                        (state == RequestContextBase::REQUEST ? "REQUEST" :
                         state == RequestContextBase::WRITE   ? "WRITE"   :
                         state == RequestContextBase::FINISH  ? "FINISH"  : "UNKNOWN"));
            }

            // Ask the system start processing requests
            StartProcessing(srv);
        }
    };

    //
    // Implementation
    //
    void Stop();
    void ThreadProc(int threadIndex);

    //
    // Class data
    //
    std::unique_ptr<Server> server_;
    std::string serverAddress_;
    GrpcService::AsyncService service_;
    std::unique_ptr<ServerCompletionQueue> cq_;

    std::vector<std::thread> threads_;
    int threadCount_ = 0;
};

//
//  Handle for SIGHUP & SIGINT signals
//
extern "C" void HandleHangupSignal(int signalNumber)
{
    INFOMSG_MT("Got a signal " << signalNumber << ", exiting...");
    _exit(1); // TODO
}

//
// GrpcServer implementation
//
void GrpcServer::Run()
{
    // Handle SIGHUP and SIGINT in the main process,
    // but not in any of the threads that are spawned.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

    struct sigaction act;
    act.sa_handler = HandleHangupSignal;
    sigaction(SIGHUP, &act, nullptr);
    sigaction(SIGINT, &act, nullptr);

    INFOMSG_MT("serverAddress_ = " << serverAddress_);

    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(serverAddress_, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    //builder.SetMaxSendMessageSize(GRPC_MAX_MESSAGE_SIZE);     // For testing max message size
    //builder.SetMaxReceiveMessageSize(GRPC_MAX_MESSAGE_SIZE);  // For testing max message size

    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    // Create contexts for the server threads.
    // NOTE: In the gRpc code grpc_1.0.0/test/cpp/end2end/thread_stress_test.cc
    // the number of contexts are multiple to 100 to the number of threads.
    // This defines the number of simultaneous completion queue requests
    // of the same type (through GrpcService::AsyncService::RequestXXX).
    // Presuming fast application response (through GrpcServer::XXX),
    // this approach should be sufficient.
    //int context_count = threadCount_ * 100;
    int context_count = threadCount_ * 10;

    PROCESS_STREAM(StreamTestRequest, StreamTestResponse, StreamTest, StreamTest, context_count)
    PROCESS_UNARY(ShutdownRequest, ShutdownResponse, Shutdown, Shutdown, context_count)
    PROCESS_UNARY(PingRequest, PingResponse, Ping, Ping, context_count)

    // Start server threads
    threads_.resize(threadCount_);
    int threadIndx = 0;
    for(std::thread& thread : threads_)
    {
        thread = std::thread(&GrpcServer::ThreadProc, this, threadIndx++);
    }

    INFOMSG_MT("Grpc Server is running with " << threads_.size() << " threads");

    // Loop until GrpcServerImpl::OnRun()returns
    while(GrpcServerImpl::OnRun())
    {
        sleep(2); // Sleep for 2 seconds and continue
    }

    INFOMSG_MT("Stopping Grpc Server...");

    Stop();
}

void GrpcServer::Stop()
{
    server_->Shutdown();
    cq_->Shutdown();

    INFOMSG_MT("Waiting for server threads to complete...");

    for(std::thread& thread : threads_)
    {
        thread.join();
    }
    threads_.clear();

    INFOMSG_MT("All server threads are completed");

    // Ignore all remaining events
    void* ignored_tag = nullptr;
    bool ignored_ok = false;
    while (cq_->Next(&ignored_tag, &ignored_ok))
        ;

    // We are done
}

void GrpcServer::ThreadProc(int threadIndex)
{
    // PR5044360: Don't handle SIGHUP or SIGINT in the spawned threads -
    // let the main process handle them.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    void* tag = nullptr;
    bool eventReadSuccess = false;

    while(cq_->Next(&tag, &eventReadSuccess))
    {
        if(tag == nullptr)
        {
            ERRORMSG_MT("Server Completion Queue returned empty tag");
            continue;
        }

        // Get the request context for the specific tag
        RequestContextBase* ctx = static_cast<RequestContextBase*>(tag);

//        OUTMSG_MT("Next Event: tag='" << tag << "', eventReadSuccess=" << eventReadSuccess << ", "
//                << "state=" << (ctx->state == RequestContextBase::REQUEST ? "REQUEST" :
//                                ctx->state == RequestContextBase::WRITE   ? "WRITE"   :
//                                ctx->state == RequestContextBase::FINISH  ? "FINISH"  : "UNKNOWN"));

        // Have we read event successfully?
        if(!eventReadSuccess)
        {
            // Ignore events that failed to read due to shutting down
            if(ctx->state != RequestContextBase::REQUEST)
            {
                ERRORMSG_MT("Server Completion Queue failed to read event for tag '" << tag << "'");

                // Abort processing if we failed reading event
                ctx->EndProcessing(this, true /*isError*/);
            }
            continue;
        }

        // Process the event
        switch(ctx->state)
        {
            case RequestContextBase::REQUEST:  // Completion of fRequestPtr()
            case RequestContextBase::WRITE:    // Completion of Write()
                // Process request
                ctx->Process(this);
                break;

            case RequestContextBase::FINISH:    // Completion of Finish()
                // Process post-Finish() event
                ctx->EndProcessing(this, false /*isError*/);
                break;

            default:
                ERRORMSG_MT("Unknown Completion Queue event: "
                    << "ctx->state=" << ctx->state << ", tag='" << tag << "'");
                //abort();
                break;
        } // end of switch
    } // end of while

    INFOMSG_MT("Thread " << threadIndex << " is completed");
}

// Global function to create and run Grpc Server
bool StartGrpcServer(unsigned short port, int threadCount)
{
    // Build & start gRpc server
    std::string serverAddress = "0.0.0.0:" + std::to_string(port);
    GrpcServer srv(serverAddress.c_str(), threadCount);
    srv.Run();
    return true;
}



// *INDENT-OFF*
//
// grpcServer.cpp
//
#include <thread>
#include <signal.h>             // pthread_sigmask
#include <unistd.h>             // sleep
#include "logger.hpp"

#include "grpcServer.hpp"

namespace gen {

//
//  Signal handler for SIGHUP & SIGINT signals
//
extern "C"
{
    static void HandleHangupSignal(int /*signalNumber*/)
    {
//        INFOMSG(Con(), strsignal(signalNumber)
//            << " signal (" << signalNumber << ") received, exiting...");
        exit(1); // TODO
    }
}

//
// Class RpcContext implementation
//
void RpcContext::SetStatus(::grpc::StatusCode statusCode, const std::string& err) const
{
    grpcStatusCode = statusCode;

    // Note: Ignore err if status is grpc::OK. Otherwise, it will be
    // an error to construct gen::Status::OK with non-empty error_message.
    if(statusCode != grpc::OK)
        grpcErr = err;
}

void RpcContext::GetMetadata(const char* key, std::string& value) const
{
    assert(srvCtx);
    const std::multimap<::grpc::string_ref, ::grpc::string_ref>& client_metadata = srvCtx->client_metadata();
    auto itr = client_metadata.find(key);
    if(itr != client_metadata.end())
        value.assign(itr->second.data(), itr->second.size());
}

void RpcContext::SetMetadata(const char* key, const std::string& value) const
{
    assert(srvCtx);
    srvCtx->AddTrailingMetadata(key, value);
}

std::string RpcContext::Peer() const
{
    assert(srvCtx);
    return srvCtx->peer();
}

//
// Class GrpcServer implementation
//
void GrpcServer::Run(unsigned short port, int threadCount)
{
    // PR5044360: Handle SIGHUP and SIGINT in the main process,
    // but not in any of the threads that are spawned.

    // Examine the current set of the blocked signals
    sigset_t curset;
    pthread_sigmask(0, nullptr, &curset);

    // Is either SIGHUP or SIGINT blocked?
    bool isBlockedSIGHUP = (sigismember(&curset, SIGHUP) == 1);
    bool isBlockedSIGINT = (sigismember(&curset, SIGINT) == 1);

    // Unblock SIGHUP & SIGINT if either is blocked
    if(isBlockedSIGHUP || isBlockedSIGINT)
    {
        // Note: It is permissible to unblock a signal which is not blocked.
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGHUP);
        sigaddset(&set, SIGINT);
        pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
    }

    // Set SIGHUP & SIGINT signal handler
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = HandleHangupSignal;

    struct sigaction oldactSIGHUP;
    sigaction(SIGHUP, &act, &oldactSIGHUP);

    struct sigaction oldactSIGINT;
    sigaction(SIGINT, &act, &oldactSIGINT);

    // Run the server
    RunImpl(port, threadCount);

    // If SIGHUP was blocked but we have it unblocked, then block it back
    if(isBlockedSIGHUP)
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGHUP);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
    }

    // If SIGINT was blocked but we have it unblocked, then block it back
    if(isBlockedSIGINT)
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
    }

    // Restore sigactions for SIGHUP and SIGINT
    sigaction(SIGHUP, &oldactSIGHUP, nullptr);
    sigaction(SIGINT, &oldactSIGINT, nullptr);
}

void GrpcServer::RunImpl(unsigned short port, int threadCount)
{
    // Get the number of contexts for the server threads.
    // NOTE: In the gRpc code grpc_1.0.0/test/cpp/end2end/thread_stress_test.cc
    // the number of contexts are multiple to 100 to the number of threads.
    // This defines the number of simultaneous completion queue requests
    // of the same type (through SessionManagerService::AsyncService::RequestXXX).
    // Presuming fast application response (through genGrpcServer::XXX),
    // this approach should be sufficient.
    //int context_count = threadCount * 100;
    contextCount_ = threadCount * 10;

    while(true)
    {
        // Call derived class initialization
        if(!OnInit())
        {
            OnError("Server inialization failed");
            break;
        }
        else if(serviceList_.empty())
        {
            OnError("Server inialization failed: no services registered");
            break;
        }
        else if(requestContextList_.empty())
        {
            OnError("Server inialization failed: no RPC request registered");
            break;
        }

        // Setup server
        std::string serverAddress = "0.0.0.0:" + std::to_string(port);
        OnInfo("serverAddress_ = " + serverAddress);

        ::grpc::ServerBuilder builder;
        builder.AddListeningPort(serverAddress, ::grpc::InsecureServerCredentials());

        // Register services
        for(GrpcService* srv : serviceList_)
        {
            builder.RegisterService(srv->service->get());
        }

        std::unique_ptr<::grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
        std::unique_ptr<::grpc::Server> server = builder.BuildAndStart();

        // Ask the system start processing requests
        for(RequestContext* ctx : requestContextList_)
        {
            ctx->StartProcessing(cq.get());
        }

        // Start threads
        std::vector<std::thread> threads(threadCount);

        int threadIndx = 0;
        for(std::thread& thread : threads)
        {
            thread = std::thread(&GrpcServer::ProcessRpcsProc, this, cq.get(), threadIndx++);
        }

        OnInfo("GrpcServer is running with " + std::to_string(threads.size()) + " threads");

        // Loop until OnRun()returns
        while(OnRun())
        {
            sleep(2); // Sleep for 2 seconds and continue
        }

        OnInfo("Stopping Session GrpcServer Server...");

        server->Shutdown();
        cq->Shutdown();

        OnInfo("Waiting for server threads to complete...");

        for(std::thread& thread : threads)
        {
            thread.join();
        }
        threads.clear();

        OnInfo("All server threads are completed");

        // Ignore all remaining events
        void* ignored_tag = nullptr;
        bool ignored_ok = false;
        while (cq->Next(&ignored_tag, &ignored_ok))
            ;

        // We are done
        break;
    }

    // Clean up...
    for(GrpcService* srv : serviceList_)
    {
        delete srv->service;
        srv->service = nullptr;
    }
    serviceList_.clear();

    for(RequestContext* ctx : requestContextList_)
    {
        delete ctx;
    }
    requestContextList_.clear();

    contextCount_ = 0;
}

void GrpcServer::ProcessRpcsProc(::grpc::ServerCompletionQueue* cq, int threadIndex)
{
    // PR5044360: Don't handle SIGHUP or SIGINT in the spawned threads -
    // let the main thread handle them.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    void* tag = nullptr;
    bool eventReadSuccess = false;

    while(cq->Next(&tag, &eventReadSuccess))
    {
        if(tag == nullptr)
        {
            std::cout << "#### tag=NULL" << std::endl;

            OnError("Server Completion Queue returned empty tag");
            continue;
        }

        // Get the request context for the specific tag
        RequestContext* ctx = static_cast<RequestContext*>(tag);

//        // victor test
//        OUTMSG_MT("Next Event: tag='" << tag << "', eventReadSuccess=" << eventReadSuccess << ", "
//                << "state=" << (ctx->state == RequestContext::REQUEST ? "REQUEST" :
//                                ctx->state == RequestContext::READ    ? "READ"    :
//                                ctx->state == RequestContext::WRITE   ? "WRITE"   :
//                                ctx->state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN"));

        // Have we successfully read event?
        if(!eventReadSuccess)
        {
            // If we are reading from the client stream, then
            // this means we don't have any more messsges to read
            if(ctx->state == RequestContext::READ)
            {
                // Done reading client-streaming messages
                ctx->state = RequestContext::READEND;
                ctx->Process();
                continue;
            }
            // Ignore events that failed to read due to shutting down
            else if(ctx->state != RequestContext::REQUEST)
            {
                std::stringstream ss;
                ss << "Server Completion Queue failed to read event for tag '" << tag << "'";
                OnError(ss.str());

                // Abort processing if we failed reading event
                ctx->EndProcessing(this, cq, true /*isError*/);
            }
            continue;
        }

        // Process the event
        switch(ctx->state)
        {
        case RequestContext::REQUEST:  // Completion of fRequestPtr()
        case RequestContext::READ:     // Completion of Read()
        case RequestContext::WRITE:    // Completion of Write()
            // Process request
            ctx->Process();
            break;

        case RequestContext::FINISH:    // Completion of Finish()
            // Process post-Finish() event
            ctx->EndProcessing(this, cq, false /*isError*/);
            break;

        default:
            std::stringstream ss;
            ss << "Unknown Completion Queue event: ctx->state=" << ctx->state << ", tag='" << tag << "'";
            OnError(ss.str());
            break;
        } // end of switch
    } // end of while

    OnInfo("Thread " + std::to_string(threadIndex) + " is completed");
}

//void GrpcServer::ProcessRpcsProcAsync()
//{
//    pthread_t thread_id = pthread_self();
//
//    gpr_timespec timeout = { 2, 0, GPR_TIMESPAN }; // 2 seconds
//
//    void* tag = nullptr; // Uniquely identifies a request.
//    bool ok = false;
//
//    while(true)
//    {
//        ::grpc::ServerCompletionQueue::NextStatus st = cq_->AsyncNext(&tag, &ok, timeout);
//
//        if(st == ::grpc::ServerCompletionQueue::TIMEOUT)
//        {
//            printf("[Thread %li]: TIMEOUT\n", thread_id);
//            continue;
//        }
//        else if(st == ::grpc::ServerCompletionQueue::GOT_EVENT)
//        {
//            //printf("[Thread %li]: GOT_EVENT\n", thread_id);
//
//            // victor test
//            static int false_count = 0;
//            if(!ok)
//            {
//                false_count++;
//                if(false_count < 20)
//                    printf("[Thread %li]: GOT_EVENT ok=false, tag=%p\n", thread_id, tag);
//            }
//            else
//            {
//                printf("[Thread %li]: GOT_EVENT\n", thread_id);
//            }
//            // victor test end
//
//            if(ok && tag != nullptr)
//            {
//                Contex* ctx = static_cast<Contex*>(tag);
//
//                if(ctx->state == Contex::READY)
//                {
//                    // Process request
//                    ctx->Process(this, thread_id);
//                }
//                else if(ctx->state == Contex::DONE)
//                {
//                    // *Request* that the system start processing requests
//                    ctx->StartProcessing(this);
//                }
//                else
//                {
//                    // TODO
//                    GPR_ASSERT(false);
//                }
//            }
//        }
//        else if(st == ::grpc::ServerCompletionQueue::SHUTDOWN)
//        {
//            printf("[Thread %li]: SHUTDOWN: exiting...\n", thread_id);
//            break;
//        }
//    }
//}

} //namespace gen

// *INDENT-ON*

// *INDENT-OFF*
//
// grpcServer.hpp
//
#ifndef __GRPC_SERVER_HPP__
#define __GRPC_SERVER_HPP__

// Note: The multithreaded gRpc server is implemented Based on
//       grpc_1.0.0/test/cpp/end2end/thread_stress_test.cc
//

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/service_type.h>
#pragma GCC diagnostic pop

#include "grpcContext.hpp"  // RpcContext
#include "grpcUtils.hpp"
#include <sstream>          // stringstream
#include <thread>
#include <signal.h>         // pthread_sigmask
#include <unistd.h>         // usleep

namespace gen {

class GrpcServer;

//
// Base request context class
//
struct RequestContext
{
    RequestContext() = default;
    virtual ~RequestContext() = default;

    std::unique_ptr<::grpc::ServerContext> srv_ctx;
    enum : char { UNKNOWN=0, REQUEST, READ, READEND, WRITE, FINISH } state = UNKNOWN;
    GrpcService* grpcService = nullptr;

    // Any application-level data assigned by AddRpcRequest.
    // It will be make available to RPC function through RpcContext.
    const void* processParam = nullptr; // Any application-level data stored by AddRpcRequest

    virtual void Process() = 0;
    virtual void StartProcessing(::grpc::ServerCompletionQueue* cq) = 0;
    virtual void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError) = 0;

    virtual RequestContext* CloneMe() const = 0;
};

//
// Template service-specific AsyncService type
//
template<class RPC_SERVICE>
using AsyncService = typename RPC_SERVICE::AsyncService;

//
// Template pointer to function that does actual unary/stream processing
//
template<class REQ, class RESP>
using ProcessUnaryFunc = void (GrpcService::*)(const RpcContext&, const REQ&, RESP&);

template<class REQ, class RESP>
using ProcessServerStreamFunc = void (GrpcService::*)(const RpcServerStreamContext&, const REQ&, RESP&);

template<class REQ, class RESP>
using ProcessClientStreamFunc = void (GrpcService::*)(const RpcClientStreamContext&, const REQ&, RESP&);

//
// Template pointer to function that *request* the system to start processing unary.strean requests
//
template<class RPC_SERVICE, class REQ, class RESP>
using UnaryRequestFuncPtr = void (AsyncService<RPC_SERVICE>::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncResponseWriter<RESP>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<class RPC_SERVICE, class REQ, class RESP>
using ServerStreamRequestFuncPtr = void (AsyncService<RPC_SERVICE>::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncWriter<RESP>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<class RPC_SERVICE, class REQ, class RESP>
using ClientStreamRequestFuncPtr = void (AsyncService<RPC_SERVICE>::*)(::grpc::ServerContext*,
        ::grpc::ServerAsyncReader<RESP, REQ>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

//
// Class GrpcServer
//
struct AddressUri
{
    AddressUri(const std::string& uri_,
               std::shared_ptr<grpc::ServerCredentials> credentials_ = nullptr)
        : uri(uri_), credentials(credentials_ ? credentials_ : ::grpc::InsecureServerCredentials()) {}
    std::string uri;
    std::shared_ptr<grpc::ServerCredentials> credentials;
};

class GrpcServer
{
public:
    GrpcServer() = default;
    virtual ~GrpcServer() = default;

    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;

    // Run() is blocked. It doesn't return until OnRun() returns false.
    bool Run(unsigned short port, int threadCount,
             std::shared_ptr<grpc::ServerCredentials> credentials = nullptr)
    {
        std::vector<AddressUri> addressUriArr;
        addressUriArr.push_back({ FormatDnsAddressUri("0.0.0.0", port), credentials });
        return RunImpl(addressUriArr, threadCount);
    }

    bool Run(const char* domainSocketPath, int threadCount,
             std::shared_ptr<grpc::ServerCredentials> credentials = nullptr)
    {
        std::vector<AddressUri> addressUriArr;
        addressUriArr.push_back({ FormatUnixDomainSocketAddressUri(domainSocketPath), credentials });
        return RunImpl(addressUriArr, threadCount);
    }

    bool Run(const std::vector<AddressUri>& addressUriArr, int threadCount)
    {
        return RunImpl(addressUriArr, threadCount);
    }

    GrpcService* GetService(const std::string& serviceName)
    {
        auto it = serviceMap.find(serviceName);
        return (it == serviceMap.end() ? nullptr : it->second);
    }

    // Tell the system to process unary RPC request
    template<class RPC_SERVICE, class REQ, class RESP>
    void AddUnaryRpcRequest(GrpcService* grpcService,
                            UnaryRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc,
                            ProcessUnaryFunc<REQ, RESP> processFunc,
                            const void* processParam);

    // Tell the system to process stream RPC request
    template<class RPC_SERVICE, class REQ, class RESP>
    void AddServerStreamRpcRequest(GrpcService* grpcService,
                                   ServerStreamRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc,
                                   ProcessServerStreamFunc<REQ, RESP> processFunc,
                                   const void* processParam);

    // Tell the system to process client stream RPC request
    template<class RPC_SERVICE, class REQ, class RESP>
    void AddClientStreamRpcRequest(GrpcService* grpcService,
                                   ClientStreamRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc,
                                   ProcessClientStreamFunc<REQ, RESP> processFunc,
                                   const void* processParam);

    // Set OnRun() idle interval in milliseconds
    void SetIdleInterval(int milliseconds) { idleIntervalMicroseconds = milliseconds * 1000; }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& /*err*/) const {}
    virtual void OnInfo(const std::string& /*info*/) const {}

private:
    bool RunImpl(const std::vector<AddressUri>& addressUriArr, int threadCount)
    {
        // Get the number of contexts for the server threads.
        // NOTE: In the gRpc code grpc_1.0.0/test/cpp/end2end/thread_stress_test.cc
        // the number of contexts are multiple to 100 to the number of threads.
        // This defines the number of simultaneous completion queue requests
        // of the same type (through RPC_SERVICE::AsyncService::RequestXXX).
        // Presuming fast application response (through genGrpcServer::XXX),
        // this approach should be sufficient.
        //int context_count = threadCount * 100;
        contextCount = threadCount * 10;

        bool result = false; // Initially
        while(true)
        {
            ::grpc::ServerBuilder builder;

            // Call derived class initialization
            if(!OnInit(builder))
            {
                OnError("Server inialization failed");
                break;
            }
            else if(serviceMap.empty())
            {
                OnError("Server inialization failed: no services registered");
                break;
            }
            else if(requestContextList.empty())
            {
                OnError("Server inialization failed: no RPC request registered");
                break;
            }

            // Setup server
            for(const AddressUri& addressUri : addressUriArr)
            {
                OnInfo("addressUri = '" + addressUri.uri + "'");
                builder.AddListeningPort(addressUri.uri, addressUri.credentials);
            }

            // Register services
            for(auto& pair : serviceMap)
            {
                GrpcService* srv = pair.second;
                builder.RegisterService(srv->service);
            }

            // Add Completion Queue
            std::unique_ptr<::grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
            if(!cq)
            {
                OnError("Failed to add Completion Queue");
                break;
            }

            // Build and start server
            std::unique_ptr<::grpc::Server> server = builder.BuildAndStart();
            if(!server)
            {
                OnError("Failed to add build and start server");
                break;
            }

            // Ask the system start processing requests
            for(RequestContext* ctx : requestContextList)
            {
                ctx->StartProcessing(cq.get());
            }

            // Start threads
            std::vector<std::thread> threads(threadCount);

            int threadIndx = 0;
            for(std::thread& thread : threads)
            {
                thread = std::thread(&GrpcServer::ProcessEvents, this, cq.get(), threadIndx++);
            }

            OnInfo("GrpcServer is running with " + std::to_string(threads.size()) + " threads");

            // Loop until OnRun()returns
            while(OnRun())
            {
                usleep(idleIntervalMicroseconds);
            }

            OnInfo("Stopping GrpcServer ...");

            // Shutdown the server
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(200);
            server->Shutdown(deadline);
            //cq->Shutdown(); // grpc asserts if we shutdown cq while threads are still running
            stop = true;

            OnInfo("Waiting for server threads to complete...");

            for(std::thread& thread : threads)
            {
                thread.join();
            }
            threads.clear();

            OnInfo("All server threads are completed");

            // Shutdown and drain the completion queue
            cq->Shutdown();
            void* ignored_tag = nullptr;
            bool ignored_ok = false;
            while(cq->Next(&ignored_tag, &ignored_ok))
                ; // Ignore all remaining events

            // We are done
            result = true;
            break;
        }

        // Clean up...
        for(auto& pair : serviceMap)
        {
            GrpcService* srv = pair.second;
            delete srv->service;
            srv->service = nullptr;
        }
        serviceMap.clear();

        for(RequestContext* ctx : requestContextList)
        {
            delete ctx;
        }
        requestContextList.clear();

        contextCount = 0;
        return result;
    }

    void ProcessEvents(::grpc::ServerCompletionQueue* cq, int threadIndex)
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

        const std::chrono::milliseconds timeout(200);
        std::chrono::time_point<std::chrono::system_clock> deadline;

        // Note: Reading events's with blocking Next() doesn't work well
        // when server is being shutting down while some PRPs are still
        // in progress (grpc asserts). Non-blocking AsyncNext() works fine.
        // while(cq->Next(&tag, &eventReadSuccess))

        while(!stop)
        {
            deadline = std::chrono::system_clock::now() + timeout;
            const grpc::CompletionQueue::NextStatus status = cq->AsyncNext(&tag, &eventReadSuccess, deadline);

            if(status == grpc::CompletionQueue::NextStatus::GOT_EVENT)
            {
                // We have event to process
            }
            else if(status == grpc::CompletionQueue::NextStatus::TIMEOUT)
            {
                continue;
            }
            else if(status == grpc::CompletionQueue::NextStatus::SHUTDOWN)
            {
                break; // The server is being shutting down
            }
            else
            {
                OnError("Server Completion Queue returned invalid status" );
                continue;
            }

            if(tag == nullptr)
            {
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

    // Helpers
    template<class RPC_SERVICE>
    void AddService(GrpcService* grpcService);

    // For derived class to override
    virtual bool OnInit(::grpc::ServerBuilder& builder) = 0;
    virtual bool OnRun() = 0;

    // Class data
    int contextCount = 0;
    std::map<std::string, GrpcService*> serviceMap;
    std::list<RequestContext*> requestContextList;
    std::atomic<bool> stop{false};
    unsigned int idleIntervalMicroseconds{2000000}; // 2 secs default
};

//
// Template class to handle unary respone
//
template<class RPC_SERVICE, class REQ, class RESP>
struct UnaryRequestContext : public RequestContext
{
    UnaryRequestContext() = default;
    virtual ~UnaryRequestContext() = default;

    virtual RequestContext* CloneMe() const
    {
        UnaryRequestContext* ctx = new (std::nothrow) UnaryRequestContext;
        ctx->grpcService = grpcService;
        ctx->processParam = processParam;
        ctx->processFunc = processFunc;
        ctx->requestFunc = requestFunc;
        return ctx;
    }

    REQ req;
    std::unique_ptr<::grpc::ServerAsyncResponseWriter<RESP>> resp_writer;

    // Pointer to function that does actual processing
    ProcessUnaryFunc<REQ, RESP> processFunc = nullptr;

    // Pointer to function that *request* the system to start processing given requests
    UnaryRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc = nullptr;

    void StartProcessing(::grpc::ServerCompletionQueue* cq)
    {
        state = RequestContext::REQUEST;
        srv_ctx.reset(new ::grpc::ServerContext);
        resp_writer.reset(new ::grpc::ServerAsyncResponseWriter<RESP>(srv_ctx.get()));
        req.Clear();

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service;
        (asyncService->*requestFunc)(srv_ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process()
    {
        // The actual processing
        RpcContext rpc_ctx(srv_ctx.get(), processParam);
        RESP resp;
        (grpcService->*processFunc)(rpc_ctx, req, resp);

        // And we are done!
        ::grpc::Status grpcStatus(rpc_ctx.GetStatus(), rpc_ctx.GetError());

        // Let the gRPC runtime know we've finished, using the
        // memory address of this instance as the uniquely identifying tag for
        // the event.
        state = RequestContext::FINISH;

        resp_writer->Finish(resp, grpcStatus, this);
    }

    void EndProcessing(const GrpcServer* /*serv*/, ::grpc::ServerCompletionQueue* cq, bool /*isError*/)
    {
        // TODO
        // Handle processing errors ...

        // Ask the system start processing requests
        StartProcessing(cq);
    }
};

//
// Template class to handle streaming respone
//
template<class RPC_SERVICE, class REQ, class RESP>
struct ServerStreamRequestContext : public RequestContext
{
    ServerStreamRequestContext() = default;
    ~ServerStreamRequestContext() = default;

    virtual RequestContext* CloneMe() const
    {
        ServerStreamRequestContext* ctx = new (std::nothrow) ServerStreamRequestContext;
        ctx->grpcService  = grpcService;
        ctx->processParam = processParam;
        ctx->processFunc  = processFunc;
        ctx->requestFunc  = requestFunc;
        return ctx;
    }

    REQ req;
    std::unique_ptr<::grpc::ServerAsyncWriter<RESP>> resp_writer;
    std::unique_ptr<RpcServerStreamContext> stream_ctx;

    // Pointer to function that does actual processing
    ProcessServerStreamFunc<REQ, RESP> processFunc = nullptr;

    // Pointer to function that *request* the system to start processing given requests
    ServerStreamRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc = nullptr;

    void StartProcessing(::grpc::ServerCompletionQueue* cq)
    {
        state = RequestContext::REQUEST;
        srv_ctx.reset(new ::grpc::ServerContext);
        resp_writer.reset(new ::grpc::ServerAsyncWriter<RESP>(srv_ctx.get()));
        stream_ctx.reset();
        req.Clear();

//            // victor test
//            OUTMSG_MT(__func__ << ": Calling requestFunc(), tag='" << this << "', "
//                    << "state=" << (state == RequestContext::REQUEST ? "REQUEST" :
//                                    state == RequestContext::WRITE   ? "WRITE"   :
//                                    state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN"));

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service;
        (asyncService->*requestFunc)(srv_ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process()
    {
        if(state == RequestContext::REQUEST)
        {
            // This is very first Process call for the given request.
            state = RequestContext::WRITE;

            // Create new RpcStreamContext
            stream_ctx.reset(new RpcServerStreamContext(srv_ctx.get(), processParam));
        }

        // The actual processing
        RESP resp;
        (grpcService->*processFunc)(*stream_ctx, req, resp);

        // Are there more responses to stream?
        if(stream_ctx->streamHasMore)
        {
//                // victor test
//                OUTMSG_MT(__func__ << ": Calling Write(), tag='" << this << "', "
//                        << "state=" << (state == RequestContext::REQUEST ? "REQUEST" :
//                                        state == RequestContext::WRITE   ? "WRITE"   :
//                                        state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN"));

            resp_writer->Write(resp, this);
        }
        // There are no more responses to stream
        else
        {
            // And we are done!
            ::grpc::Status grpcStatus(stream_ctx->GetStatus(), stream_ctx->GetError());

            // Let the gRPC runtime know we've finished, using the
            // memory address of this instance as the uniquely identifying tag for
            // the event.
            state = RequestContext::FINISH;

//                // victor test
//                OUTMSG_MT(__func__ << ": Calling Finish(), tag='" << this << "', "
//                        << "state=" << (state == RequestContext::REQUEST ? "REQUEST" :
//                                        state == RequestContext::WRITE   ? "WRITE"   :
//                                        state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN"));

            resp_writer->Finish(grpcStatus, this);
        }
    }

    void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError)
    {
        if(stream_ctx)
        {
            if(isError)
            {
                const char* stateStr =
                    (state == RequestContext::REQUEST ? "REQUEST (ServerAsyncWriter::requestFunc() failed)" :
                     state == RequestContext::WRITE   ? "WRITE (ServerAsyncWriter::Write() failed)"   :
                     state == RequestContext::FINISH  ? "FINISH (ServerAsyncWriter::Finish() failed)" : "UNKNOWN");

                std::stringstream ss;
                ss << "Error streaming for tag '" << this << "', "
                   << "streamParam='" << stream_ctx->streamParam << "', state=" << stateStr;
                serv->OnError(ss.str());
            }

            // End processing
            stream_ctx->streamStatus = (isError ? StreamStatus::ERROR : StreamStatus::SUCCESS);
            RESP respDummy;
            (grpcService->*processFunc)(*stream_ctx, req, respDummy);
        }
        else
        {
            // Note: stream_ctx is set by a very first Process() call, that is called
            // after successful compleation of the event placed by requestFunc().
            // If processing of requestFunc() event failed, then we will be here
            // even before stream_ctx gets a chance to be initialized.
            // In this case we don't have a stream yet.
            const char* stateStr =
                (state == RequestContext::REQUEST ? "REQUEST" :
                 state == RequestContext::WRITE   ? "WRITE"   :
                 state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN");

            std::stringstream ss;
            ss << "Ending streaming for tag '" << this << "', "
               << "stream='Not Started', state=" << stateStr;
            serv->OnError(ss.str());
        }

        // Ask the system start processing requests
        StartProcessing(cq);
    }
};

//
// Template class to handle client streaming respone
//
template<class RPC_SERVICE, class REQ, class RESP>
struct ClientStreamRequestContext : public RequestContext
{
    ClientStreamRequestContext() = default;
    ~ClientStreamRequestContext() = default;

    virtual RequestContext* CloneMe() const
    {
        ClientStreamRequestContext* ctx = new (std::nothrow) ClientStreamRequestContext;
        ctx->grpcService = grpcService;
        ctx->processParam = processParam;
        ctx->processFunc = processFunc;
        ctx->requestFunc = requestFunc;
        return ctx;
    }

    REQ req;
    RESP resp;
    std::unique_ptr<::grpc::ServerAsyncReader<RESP, REQ>> req_reader;
    std::unique_ptr<RpcClientStreamContext> stream_ctx;

    // Pointer to function that does actual processing
    ProcessClientStreamFunc<REQ, RESP> processFunc = nullptr;

    // Pointer to function that *request* the system to start processing given requests
    ClientStreamRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc = nullptr;

    void StartProcessing(::grpc::ServerCompletionQueue* cq)
    {
        state = RequestContext::REQUEST;
        srv_ctx.reset(new ::grpc::ServerContext);
        req_reader.reset(new ::grpc::ServerAsyncReader<RESP, REQ>(srv_ctx.get()));
        stream_ctx.reset();

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service;
        (asyncService->*requestFunc)(srv_ctx.get(), req_reader.get(), cq, cq, this);
    }

    void Process()
    {
        if(state == RequestContext::REQUEST)
        {
            // This is very first Process call for the given request.
            // Create new RpcStreamContext
            stream_ctx.reset(new RpcClientStreamContext(srv_ctx.get(), processParam));
            stream_ctx->streamHasMore = true;

            // Start reading
            //OUTMSG_MT("this=" << this << ", ASK TO READ");  // victor test

            state = RequestContext::READ;
            req.Clear();
            req_reader->Read(&req, this);
        }
        else if(state == RequestContext::READ)
        {
            //OUTMSG_MT("this=" << this << ", READ COMPLETE");    // victor test

            (grpcService->*processFunc)(*stream_ctx, req, resp);

            // Is processing failed?
            if(stream_ctx->GetStatus() != ::grpc::OK)
            {
                //OUTMSG_MT("this=" << this << ", Processing returned error " << stream_ctx->GetStatus());    // victor test

                // Processing returned error
                state = RequestContext::FINISH;
                ::grpc::Status grpcStatus(stream_ctx->GetStatus(), stream_ctx->GetError());
                req_reader->FinishWithError(grpcStatus, this);
                return;
            }

            // Continue reading
            //OUTMSG_MT("this=" << this << ", ASK TO READ");  // victor test

            req.Clear();
            req_reader->Read(&req, this);
        }
        else if(state == RequestContext::READEND)
        {
            //OUTMSG_MT("this=" << this << ", READ END");     // victor test

            // And we are done!
            req.Clear();
            resp.Clear();
            state = RequestContext::FINISH;
            stream_ctx->streamHasMore = false;
            (grpcService->*processFunc)(*stream_ctx, req, resp);

            // Let the gRPC runtime know we've finished, using the
            // memory address of this instance as the uniquely identifying tag for
            // the event.
            ::grpc::Status grpcStatus(stream_ctx->GetStatus(), stream_ctx->GetError());
            req_reader->Finish(resp, grpcStatus, this);
        }
        else
        {
            //ERRORMSG_MT("Invalid ClientStreamRequestContext::Process state() " << state); // victor test

//            // TODO - handle errors
//            serv->OnError("Invalid ClientStreamRequestContext::Process state() " + std::to_string(state));
        }
    }

    void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError)
    {
        //OUTMSG_MT("Done");  // victor test

        // Ask the system start processing requests
        StartProcessing(cq);
    }
};

//
// Helper method to add service to the list
//
template<class RPC_SERVICE>
void GrpcServer::AddService(GrpcService* grpcService)
{
    // Create RPC-specific grpc service (if not created yet) and
    // bind it with the corresponding processing function.
    if(grpcService->service == nullptr)
    {
        // Note: service_full_name() is not well documented, but
        // is generated for every service class. It might need to
        // be replaced if/when it's no longer generated.
        const char* serviceName = RPC_SERVICE::service_full_name();
        grpcService->service = new (std::nothrow) typename RPC_SERVICE::AsyncService;
        grpcService->serviceName = serviceName;
        serviceMap[serviceName] = grpcService;
//        std::cout << ">>> " << __func__ << ":"
//                << " name='" << grpcService->serviceName << "',"
//                << " service=" << grpcService->service << std::endl;
    }
}

//
// GrpcServer::AddUnaryRpcRequest implementation
//
template<class RPC_SERVICE, class REQ, class RESP>
void GrpcServer::AddUnaryRpcRequest(GrpcService* grpcService,
                                    UnaryRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc,
                                    ProcessUnaryFunc<REQ, RESP> processFunc,
                                    const void* processParam)
{
    // Create RPC-specific grpc service (if not created yet) and
    // bind it with the corresponding processing function.
    AddService<RPC_SERVICE>(grpcService);

    UnaryRequestContext<RPC_SERVICE, REQ, RESP> ctx;
    ctx.grpcService  = grpcService;
    ctx.processParam = processParam;
    ctx.processFunc  = processFunc;
    ctx.requestFunc  = requestFunc;

    // Add request contexts
    for(int i = 0; i < contextCount; i++)
    {
        requestContextList.push_back(ctx.CloneMe());
    }
}

//
// GrpcServer::AddStreamRpcRequest implementation
//
template<class RPC_SERVICE, class REQ, class RESP>
void GrpcServer::AddServerStreamRpcRequest(GrpcService* grpcService,
                                           ServerStreamRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc,
                                           ProcessServerStreamFunc<REQ, RESP> processFunc,
                                           const void* processParam)
{
    // Create RPC-specific grpc service (if not created yet) and
    // bind it with the corresponding processing function.
    AddService<RPC_SERVICE>(grpcService);

    ServerStreamRequestContext<RPC_SERVICE, REQ, RESP> ctx;
    ctx.grpcService = grpcService;
    ctx.processParam = processParam;
    ctx.processFunc = processFunc;
    ctx.requestFunc = requestFunc;

    // Add request contexts
    for(int i = 0; i < contextCount; i++)
    {
        requestContextList.push_back(ctx.CloneMe());
    }
}

//
// GrpcServer::AddClientStreamRpcRequest implementation
//
template<class RPC_SERVICE, class REQ, class RESP>
void GrpcServer::AddClientStreamRpcRequest(GrpcService* grpcService,
                                           ClientStreamRequestFuncPtr<RPC_SERVICE, REQ, RESP> requestFunc,
                                           ProcessClientStreamFunc<REQ, RESP> processFunc,
                                           const void* processParam)
{
    // Create RPC-specific grpc service (if not created yet) and
    // bind it with the corresponding processing function.
    AddService<RPC_SERVICE>(grpcService);

    ClientStreamRequestContext<RPC_SERVICE, REQ, RESP> ctx;
    ctx.grpcService = grpcService;
    ctx.processParam = processParam;
    ctx.processFunc = processFunc;
    ctx.requestFunc = requestFunc;

    // Add request contexts
    for(int i = 0; i < contextCount; i++)
    {
        requestContextList.push_back(ctx.CloneMe());
    }
}

} //namespace gen

//
// Helper macros to add unary and stream RPC request.
// Should be called within GrpcService::Init() implementation
//
#define ADD_UNARY(RPC, REQ, RESP, RPC_SERVICE, PROC_FUNC_PTR, PROC_FUNC_PARAM, SERVER_PTR)  \
    SERVER_PTR->AddUnaryRpcRequest<RPC_SERVICE, REQ, RESP>(this,                            \
            &RPC_SERVICE::AsyncService::Request##RPC,                                       \
            (gen::ProcessUnaryFunc<REQ, RESP>)PROC_FUNC_PTR,                                \
            PROC_FUNC_PARAM);

#define ADD_SERVER_STREAM(RPC, REQ, RESP, RPC_SERVICE, PROC_FUNC_PTR, PROC_FUNC_PARAM, SERVER_PTR) \
    SERVER_PTR->AddServerStreamRpcRequest<RPC_SERVICE, REQ, RESP>(this,                            \
            &RPC_SERVICE::AsyncService::Request##RPC,                                              \
            (gen::ProcessServerStreamFunc<REQ, RESP>)PROC_FUNC_PTR,                                \
            PROC_FUNC_PARAM);

#define ADD_CLIENT_STREAM(RPC, REQ, RESP, RPC_SERVICE, PROC_FUNC_PTR, PROC_FUNC_PARAM, SERVER_PTR) \
    SERVER_PTR->AddClientStreamRpcRequest<RPC_SERVICE, REQ, RESP>(this,                            \
            &RPC_SERVICE::AsyncService::Request##RPC,                                              \
            (gen::ProcessClientStreamFunc<REQ, RESP>)PROC_FUNC_PTR,                                \
            PROC_FUNC_PARAM);

#endif // __GRPC_SERVER_HPP__
// *INDENT-ON*


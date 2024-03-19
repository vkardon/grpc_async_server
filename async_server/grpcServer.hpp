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

#if 0
#define TRACE(msg) \
do{ \
    std::stringstream buf; \
    buf << "[INTERNAL] " << msg; \
    OnInfo(buf.str()); \
}while(0)
#endif

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

    // Any application-level data assigned by AddRpcRequest.
    // It will be make available to RPC function through RpcContext.
    const void* processParam = nullptr; // Any application-level data stored by AddRpcRequest

    virtual void Process() = 0;
    virtual void StartProcessing(::grpc::ServerCompletionQueue* cq) = 0;
    virtual void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError) = 0;

    virtual RequestContext* Clone() = 0;
};

//
// This is the base class for service-specific RPC-processing classes
//
class GrpcServiceBase
{
public:
    virtual ~GrpcServiceBase() = default;
    virtual bool OnInit() = 0;
    virtual bool IsServing() { return true; }
    const char* GetName() { return serviceName; }

private:
    virtual bool Init() = 0;

protected:
    std::unique_ptr<::grpc::Service> service;
    const char* serviceName = "";
    GrpcServer* srv = nullptr;

    friend class GrpcServer;

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct UnaryRequestContext;

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct ServerStreamRequestContext;

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct ClientStreamRequestContext;
};

template<class RPC_SERVICE>
class GrpcService;

//
// Template service-specific AsyncService type
//
template<class RPC_SERVICE>
using AsyncService = typename RPC_SERVICE::AsyncService;

//
// Template pointer to function that does actual unary/stream processing
//
template<class RPC_SERVICE, class REQ, class RESP>
using ProcessUnaryFunc = void (GrpcService<RPC_SERVICE>::*)(const RpcContext&, const REQ&, RESP&);

template<class RPC_SERVICE, class REQ, class RESP>
using ProcessServerStreamFunc = void (GrpcService<RPC_SERVICE>::*)(const RpcServerStreamContext&, const REQ&, RESP&);

template<class RPC_SERVICE, class REQ, class RESP>
using ProcessClientStreamFunc = void (GrpcService<RPC_SERVICE>::*)(const RpcClientStreamContext&, const REQ&, RESP&);

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

    // Run() is blocked. It doesn't return until Shutdown() is called.
    bool Run(unsigned short port, int threadCount,
             std::shared_ptr<grpc::ServerCredentials> credentials = nullptr)
    {
        std::vector<AddressUri> addressUriArr;
        addressUriArr.push_back({ FormatDnsAddressUri("0.0.0.0", port), credentials });
        return RunImpl(addressUriArr, threadCount);
    }

    bool Run(const std::string& addressUri, int threadCount)
    {
        std::vector<AddressUri> addressUriArr;
        addressUriArr.push_back(addressUri);
        return RunImpl(addressUriArr, threadCount);
    }

    bool Run(const std::vector<AddressUri>& addressUriArr, int threadCount)
    {
        return RunImpl(addressUriArr, threadCount);
    }

    void Shutdown() { runService = false; }
    bool IsRunning() { return runService; }

    template<class GRPC_SERVICE, class... SERVICE_ARGS>
    GRPC_SERVICE* AddService(SERVICE_ARGS&&...args)
    {
        GRPC_SERVICE* grpcService = new (std::nothrow) GRPC_SERVICE(args...);
        grpcService->srv = this;
        serviceMap[grpcService->GetName()].reset(grpcService);
        return grpcService;
    }

    GrpcServiceBase* GetService(const std::string& serviceName)
    {
        auto it = serviceMap.find(serviceName);
        return (it == serviceMap.end() ? nullptr : it->second.get());
    }

    // Set OnRun() call interval in milliseconds
    void SetRunInterval(int milliseconds) { runIntervalMicroseconds = milliseconds * 1000; }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& /*err*/) const {}
    virtual void OnInfo(const std::string& /*info*/) const {}

private:
    bool RunImpl(const std::vector<AddressUri>& addressUriArr, int threadCount)
    {
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

            // Do we have any services?
            if(serviceMap.empty())
            {
                OnError("Server inialization failed: no services registered");
                break;
            }

            // Call services derived class initialization
            bool serviceInitResult = true;
            for(auto& pair : serviceMap)
            {
                GrpcServiceBase* grpcService = pair.second.get();
                if(!grpcService->Init())
                {
                    serviceInitResult = false;
                    const std::string& serviceName = pair.first;
                    OnError("Init() failed for service '" + serviceName + "'");
                    break;
                }
            }
            if(!serviceInitResult)
                break; // Some service failed to initialize

            // Make sure we have registered RPC requests
            if(requestContextList.empty())
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
                // Note: Only register service once. This would be the case
                // when gRpc server stopped and then started again.
                GrpcServiceBase* grpcService = pair.second.get();
                builder.RegisterService(grpcService->service.get());
            }

            // Add Completion Queues - one queue per a thread for a best performance
            std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqueus;
            for(int i = 0; i < threadCount; i++)
            {
                cqueus.emplace_back(builder.AddCompletionQueue());
                if(!cqueus.back())
                    break;
            }

            if((int)cqueus.size() != threadCount)
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

            // Start threads
            runThreads = true;
            std::vector<std::thread> threads;
            for(int i = 0; i < threadCount; i++)
            {
                threads.emplace_back(&GrpcServer::ProcessEvents, this, cqueus[i].get(), i);
            }

            OnInfo("GrpcServer is running with " + std::to_string(threads.size()) + " threads");

            // Loop until runService is true
            runService = true;
            while(runService)
            {
                OnRun();
                usleep(runIntervalMicroseconds);
            }

            OnInfo("Stopping GrpcServer ...");

            // Shutdown the server
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(200);
            server->Shutdown(deadline);

            OnInfo("Waiting for server threads to complete...");

            runThreads = false;
            for(std::thread& thread : threads)
            {
                thread.join();
            }
            threads.clear();

            OnInfo("All server threads are completed");

            // We are done
            result = true;
            break;
        }

        // Clean up...
        requestContextList.clear();
        runService = false;
        runThreads = false;

        return result;
    }

    void ProcessEvents(::grpc::ServerCompletionQueue* cq, int threadIndex)
    {
        // Don't handle SIGHUP or SIGINT in the spawned threads -
        // let the main thread handle them.
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGHUP);
        sigaddset(&set, SIGINT);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);

        // Ask the system start processing requests
        std::list<std::unique_ptr<RequestContext>> threadRequestContextList;
        for(const std::unique_ptr<RequestContext>& ctx : requestContextList)
        {
            RequestContext* threadRequestContext = ctx->Clone();
            threadRequestContextList.emplace_back(threadRequestContext);
            threadRequestContext->StartProcessing(cq);
        }

        // Enter event loop to process events
        void* tag = nullptr;
        bool eventReadSuccess = false;

        const std::chrono::milliseconds timeout(200);
        std::chrono::time_point<std::chrono::system_clock> deadline;

        // Note: Reading events's with blocking Next() doesn't work well
        // when server is being shutting down while some PRPs are still
        // in progress (grpc asserts). Non-blocking AsyncNext() works fine.
        // while(cq->Next(&tag, &eventReadSuccess))

        while(runThreads)
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
    //        TRACE("Next Event: tag='" << tag << "', eventReadSuccess=" << eventReadSuccess << ", "
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

        // Shutdown and drain the completion queue
        cq->Shutdown();
        while(cq->Next(&tag, &eventReadSuccess))
            ; // Ignore all remaining events

        OnInfo("Thread " + std::to_string(threadIndex) + " is completed");
    }

    // Helpers
    void AddRpcRequest(RequestContext* ctx) { requestContextList.emplace_back(ctx); }

    // For derived class to override
    virtual bool OnInit(::grpc::ServerBuilder& builder) = 0;
    virtual void OnRun() = 0;

    // Class data
    std::map<std::string, std::unique_ptr<GrpcServiceBase>> serviceMap;
    std::list<std::unique_ptr<RequestContext>> requestContextList;
    std::atomic<bool> runService{false};            // Initially, since we are not running yet
    std::atomic<bool> runThreads{false};            // Initially, since we don't have any threads yet
    unsigned int runIntervalMicroseconds{1000000};  // 1 secs default

    template<class RPC_SERVICE>
    friend class GrpcService;
};

//
// Template class to handle unary respone
//
template<class RPC_SERVICE, class REQ, class RESP>
struct UnaryRequestContext : public RequestContext
{
    UnaryRequestContext() = default;
    virtual ~UnaryRequestContext() = default;

    GrpcService<RPC_SERVICE>* grpcService = nullptr;
    REQ req;
    std::unique_ptr<::grpc::ServerAsyncResponseWriter<RESP>> resp_writer;

    // Pointer to function that does actual processing
    ProcessUnaryFunc<RPC_SERVICE, REQ, RESP> processFunc = nullptr;

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
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service.get();
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

    virtual RequestContext* Clone()
    {
        auto ctx = new (std::nothrow) UnaryRequestContext<RPC_SERVICE, REQ, RESP>;
        ctx->grpcService = grpcService;
        ctx->requestFunc = requestFunc;
        ctx->processFunc = processFunc;
        ctx->processParam = processParam;
        return ctx;
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

    GrpcService<RPC_SERVICE>* grpcService = nullptr;
    REQ req;
    std::unique_ptr<::grpc::ServerAsyncWriter<RESP>> resp_writer;
    std::unique_ptr<RpcServerStreamContext> stream_ctx;

    // Pointer to function that does actual processing
    ProcessServerStreamFunc<RPC_SERVICE, REQ, RESP> processFunc = nullptr;

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
//            TRACE("Calling requestFunc(), tag='" << this << "', "
//                    << "state=" << (state == RequestContext::REQUEST ? "REQUEST" :
//                                    state == RequestContext::WRITE   ? "WRITE"   :
//                                    state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN"));

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service.get();
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
//                TRACE("Calling Write(), tag='" << this << "', "
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
//                TRACE("Calling Finish(), tag='" << this << "', "
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

    virtual RequestContext* Clone()
    {
        auto ctx = new (std::nothrow) ServerStreamRequestContext<RPC_SERVICE, REQ, RESP>;
        ctx->grpcService = grpcService;
        ctx->requestFunc = requestFunc;
        ctx->processFunc = processFunc;
        ctx->processParam = processParam;
        return ctx;
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

    GrpcService<RPC_SERVICE>* grpcService = nullptr;
    REQ req;
    RESP resp;
    std::unique_ptr<::grpc::ServerAsyncReader<RESP, REQ>> req_reader;
    std::unique_ptr<RpcClientStreamContext> stream_ctx;

    // Pointer to function that does actual processing
    ProcessClientStreamFunc<RPC_SERVICE, REQ, RESP> processFunc = nullptr;

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
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service.get();
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
            //TRACE("this=" << this << ", ASK TO READ");  // victor test

            state = RequestContext::READ;
            req.Clear();
            req_reader->Read(&req, this);
        }
        else if(state == RequestContext::READ)
        {
            //TRACE("this=" << this << ", READ COMPLETE");    // victor test

            (grpcService->*processFunc)(*stream_ctx, req, resp);

            // Is processing failed?
            if(stream_ctx->GetStatus() != ::grpc::OK)
            {
                //TRACE("this=" << this << ", Processing returned error " << stream_ctx->GetStatus());    // victor test

                // Processing returned error
                state = RequestContext::FINISH;
                ::grpc::Status grpcStatus(stream_ctx->GetStatus(), stream_ctx->GetError());
                req_reader->FinishWithError(grpcStatus, this);
                return;
            }

            // Continue reading
            //TRACE("this=" << this << ", ASK TO READ");  // victor test

            req.Clear();
            req_reader->Read(&req, this);
        }
        else if(state == RequestContext::READEND)
        {
            //TRACE("this=" << this << ", READ END");     // victor test

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
            //ERRORMSG("Invalid ClientStreamRequestContext::Process state() " << state); // victor test

//            // TODO - handle errors
//            serv->OnError("Invalid ClientStreamRequestContext::Process state() " + std::to_string(state));
        }
    }

    void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError)
    {
        //TRACE("Done");  // victor test

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone()
    {
        auto ctx = new (std::nothrow) ClientStreamRequestContext<RPC_SERVICE, REQ, RESP>;
        ctx->grpcService = grpcService;
        ctx->requestFunc = requestFunc;
        ctx->processFunc = processFunc;
        ctx->processParam = processParam;
        return ctx;
    }
};

//
// Template implementation of service-specific GrpcService class
//
template<class RPC_SERVICE>
class GrpcService : public GrpcServiceBase
{
public:
    GrpcService()
    {
        // Create RPC-specific grpc service
        // Note: service_full_name() is not well documented, but
        // is generated for every service class. It might need to
        // be replaced if/when it's no longer generated.
        serviceName = RPC_SERVICE::service_full_name();
//        std::cout << ">>> " << __func__ << ": name='" << serviceName << std::endl;

        // Note: We don't create the corresponding AsyncService here.
        // We do it in dedicated Init() method, that we called later in RunImpl(),
        // when we actually need AsyncService. This is to workaround gRpc limitation
        // to register AsyncService just once and without any way to unregister.
        // Because of that, gRpc server cannot be restarted after shutdown.
        // By calling AsyncService from RunImpl(), we destroy existing AsyncService
        // and create new one that we can register.
    }

    virtual ~GrpcService() = default;

    // Add request for unary RPC
    template<class REQ, class RESP, class SERVICE_IMPL>
    void Bind(void (SERVICE_IMPL::*processFunc)(const RpcContext&, const REQ&, RESP&),
              auto requestFunc, const void* processParam = nullptr)
    {
        // Bind RPC-specific grpc service with the corresponding processing function.
        auto ctx = new (std::nothrow) UnaryRequestContext<RPC_SERVICE, REQ, RESP>;
        ctx->grpcService = this;
        ctx->requestFunc = requestFunc;
        ctx->processFunc = (ProcessUnaryFunc<RPC_SERVICE, REQ, RESP>)processFunc;
        ctx->processParam = processParam;
        srv->AddRpcRequest(ctx);
    }

    // Add request for server-stream RPC
    template<class REQ, class RESP, class SERVICE_IMPL>
    void Bind(void (SERVICE_IMPL::*processFunc)(const RpcServerStreamContext&, const REQ&, RESP&),
              auto requestFunc, const void* processParam = nullptr)
    {
        // Bind RPC-specific grpc service with the corresponding processing function.
        auto ctx = new (std::nothrow) ServerStreamRequestContext<RPC_SERVICE, REQ, RESP>;
        ctx->grpcService = this;
        ctx->requestFunc = requestFunc;
        ctx->processFunc = (ProcessServerStreamFunc<RPC_SERVICE, REQ, RESP>)processFunc;
        ctx->processParam = processParam;
        srv->AddRpcRequest(ctx);
    }

    // Add request for client-stream RPC
    template<class REQ, class RESP, class SERVICE_IMPL>
    void Bind(void (SERVICE_IMPL::*processFunc)(const RpcClientStreamContext&, const REQ&, RESP&),
              auto requestFunc, const void* processParam = nullptr)
    {
        // Bind RPC-specific grpc service with the corresponding processing function.
        auto ctx = new (std::nothrow) ClientStreamRequestContext<RPC_SERVICE, REQ, RESP>;
        ctx->grpcService = this;
        ctx->requestFunc = requestFunc;
        ctx->processFunc = (ProcessClientStreamFunc<RPC_SERVICE, REQ, RESP>)processFunc;
        ctx->processParam = processParam;
        srv->AddRpcRequest(ctx);
    }

private:
    virtual bool Init() override final
    {
        service.reset(new (std::nothrow) AsyncService<RPC_SERVICE>);

//        std::cout << ">>> " << __func__ << ":"
//                << " name='" << serviceName << "',"
//                << " service=" << service.get() << std::endl;

        // Call derived class initialization
        return OnInit();
    }
};

} //namespace gen


#endif // __GRPC_SERVER_HPP__
// *INDENT-ON*


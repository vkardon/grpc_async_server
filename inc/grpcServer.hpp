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

#include "grpcContext.hpp"  // Context
#include "grpcUtils.hpp"    // FormatDnsAddressUri
#include <sstream>          // stringstream
#include <thread>           // std::thread
#include <signal.h>         // pthread_sigmask
#include <unistd.h>         // usleep

namespace gen {

#if 0
#define TRACE(msg) \
do{ \
    std::stringstream ss; \
    ss << "[INTERNAL " << __func__ << ":" << __LINE__ << "] " << msg; \
    OnInfo(ss.str()); \
}while(0)
#endif

//
// Base request context class
//
struct RequestContext
{
    RequestContext() = default;
    virtual ~RequestContext() = default;

    enum : char { UNKNOWN=0, REQUEST, READ, READEND, WRITE, FINISH } state = UNKNOWN;

    const char* GetStateStr()
    {
        return (state == RequestContext::REQUEST ? "REQUEST" :
                state == RequestContext::READ    ? "READ"    :
                state == RequestContext::READEND ? "READEND" :
                state == RequestContext::WRITE   ? "WRITE"   :
                state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN");
    }

    virtual void Process() = 0;
    virtual void StartProcessing(::grpc::ServerCompletionQueue* cq) = 0;
    virtual void EndProcessing(::grpc::ServerCompletionQueue* cq, bool isError) = 0;

    virtual RequestContext* Clone() = 0;
    virtual std::string_view GetRequestName() const = 0;
};

//
// This is the base class for service-specific RPC-processing classes
//
struct GrpcServiceBase
{
    // For derived class to override
    virtual bool OnInit() = 0;
    virtual const char* GetName() = 0;
    virtual ::grpc::Service* GetService() = 0;
    virtual bool IsServing() { return true; }
    virtual ~GrpcServiceBase() = default;
};

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

    bool Run(const std::string& addressUri, int threadCount,
             std::shared_ptr<grpc::ServerCredentials> credentials = nullptr)
    {
        std::vector<AddressUri> addressUriArr;
        addressUriArr.push_back({ addressUri, credentials });
        return RunImpl(addressUriArr, threadCount);
    }

    bool Run(const std::vector<AddressUri>& addressUriArr, int threadCount)
    {
        return RunImpl(addressUriArr, threadCount);
    }

    void Shutdown() { runServer = false; }
    bool IsRunning() { return isRunning; }

    template<typename GRPC_SERVICE, typename... SERVICE_ARGS>
    std::shared_ptr<GRPC_SERVICE> AddService(SERVICE_ARGS&&...args)
    {
        char const* serviceName = GRPC_SERVICE::service_full_name();

        if(!runServer)
        {
            OnError("Cannot add service '" + std::string(serviceName) + "' due to an erroneous GrpcServer state");
            return nullptr;
        }

        // Note: Use std::forward<Args>(args) to ensures that rvalue references are preserved correctly
        std::shared_ptr<GRPC_SERVICE> service(new (std::nothrow) GRPC_SERVICE(std::forward<SERVICE_ARGS>(args)...));
        if(!service)
        {
            OnError("Out of memory allocating '" + std::string(serviceName) + "' service");
            return nullptr;
        }
        // Note: Call OnInit() on GrpcServiceBase since it might be private in derived class
        else if(service->srv = this; !dynamic_cast<GrpcServiceBase*>(service.get())->OnInit())
        {
            runServer = false;  // Do not run if any of the services fail initialization
            OnError("OnInit() failed for service '" + std::string(serviceName) + "'");
            return nullptr;
        }

        serviceMap[serviceName] = service;
        return service;
    }

    GrpcServiceBase* GetService(const std::string& serviceName)
    {
        auto it = serviceMap.find(serviceName);
        return (it == serviceMap.end() ? nullptr : it->second.get());
    }

    // Set OnRun() call interval in milliseconds
    void SetRunInterval(int milliseconds) { runIntervalMicroseconds = milliseconds * 1000; }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& err) const { std::cerr << err << std::endl; }
    virtual void OnInfo(const std::string& info) const { std::cout << info << std::endl; }

private:
    bool RunImpl(const std::vector<AddressUri>& addressUriArr, int threadCount)
    {
        bool result = false;    // Initially

        while(true)
        {
            // Note: It's possible then runServer is already set to 'false' by AddService()
            if(!runServer)
            {
                OnError("Cannot continue due to an erroneous GrpcServer state");
                break;
            }

            // Call derived class initialization
            ::grpc::ServerBuilder builder;
            if(!OnInit(builder))
            {
                OnError("Server inialization failed");
                break;
            }

            // Are there any services that failed to initialize?
            if(!runServer)
            {
                OnError("Server inialization failed: some services failed to initialize");
                break;
            }

            // Do we have any services?
            if(serviceMap.empty())
            {
                OnError("Server inialization failed: no services registered");
                break;
            }

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
            for(const auto& pair : serviceMap)
            {
                // Note: Only register service once. This would be the case
                // when gRpc server stopped and then started again.
                ::grpc::Service* service = pair.second->GetService();
                builder.RegisterService(service);
            }

            // Add Completion Queues - one queue per a thread for a best performance
            std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqueues;
            for(int i = 0; i < threadCount; i++)
            {
                cqueues.emplace_back(builder.AddCompletionQueue());
                if(!cqueues.back())
                    break;
            }

            if((int)cqueues.size() != threadCount)
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
            std::vector<std::thread> threads;
            for(int i = 0; i < threadCount; i++)
            {
                threads.emplace_back(&GrpcServer::ProcessEvents, this, cqueues[i].get(), i);
            }

            OnInfo("GrpcServer is running with " + std::to_string(threads.size()) + " threads");

            // Loop until runServer is true
            isRunning = true;
            while(runServer)
            {
                OnRun();
                usleep(runIntervalMicroseconds);
            }

            OnInfo("Stopping GrpcServer ...");

            // Shutdown the server
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(200);
            server->Shutdown(deadline);
            server->Wait();  // Important: Wait for shutdown to complete

            OnInfo("Waiting for server threads to complete...");

            runThreads = false;
            for(std::thread& thread : threads)
            {
                thread.join();
            }
            threads.clear();

            OnInfo("All server threads are completed");

            // We are done
            isRunning = false;
            result = true;
            break;
        }

        // Clean up and reset to initial values
        Cleanup();
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
            if(!threadRequestContext)
            {
                OnError("Thread " + std::to_string(threadIndex) + " failed to start");
                return;
            }
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

            // victor test
//            TRACE("Next Event: tag=" << tag << ", eventReadSuccess=" << eventReadSuccess << ", state=" << GetStateStr());

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
                    // Abort processing if we failed reading event
                    std::stringstream ss;
                    ss << __func__ << ':' << __LINE__ << ' '
                       << "Failed to read Completion Queue event: state=" << ctx->GetStateStr()
                       << ", ctx=" << ctx << ", " << "req=" << ctx->GetRequestName();
                    OnError(ss.str());
                    ctx->EndProcessing(cq, true /*isError*/);
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
                ctx->EndProcessing(cq, false /*isError*/);
                break;

            default:
                std::stringstream ss;
                ss << __func__ << ':' << __LINE__ << ' '
                   << "Unknown Completion Queue event: state=" << ctx->GetStateStr()
                   << ", ctx=" << ctx << ", " << "req=" << ctx->GetRequestName();
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

    void Cleanup()
    {
        serviceMap.clear();
        requestContextList.clear();
        isRunning = false;
        runServer = true;
        runThreads = true;
    }

    // Helpers
    void AddRpcRequest(RequestContext* ctx) { requestContextList.emplace_back(ctx); }

    // For derived class to override
    virtual bool OnInit(::grpc::ServerBuilder& builder) = 0;
    virtual void OnRun() {}

    // Class data
    std::map<std::string, std::shared_ptr<GrpcServiceBase>> serviceMap;
    std::list<std::unique_ptr<RequestContext>> requestContextList;
    std::atomic<bool> isRunning{false};             // Initially, since we are not running yet
    std::atomic<bool> runServer{true};              // Initially, since we intend to run the server
    std::atomic<bool> runThreads{true};             // Initially, since we intend to run threads
    unsigned int runIntervalMicroseconds{1000000};  // 1 secs default

    template<typename RPC_SERVICE>
    friend class GrpcService;
};

template<typename RPC_SERVICE>
class GrpcService;

//
// Template pointer to function that does actual unary/stream processing
//
template<typename RPC_SERVICE, typename REQ, typename RESP>
using UnaryProcessFunc = void (GrpcService<RPC_SERVICE>::*)(const Context&, const REQ&, RESP&);

template<typename RPC_SERVICE, typename REQ, typename RESP>
using ServerStreamProcessFunc = void (GrpcService<RPC_SERVICE>::*)(const ServerStreamContext&, const REQ&, RESP&);

template<typename RPC_SERVICE, typename REQ, typename RESP>
using ClientStreamProcessFunc = void (GrpcService<RPC_SERVICE>::*)(const ClientStreamContext&, const REQ&, RESP&);

//
// Template pointer to function that *request* the system to start processing unary/strean requests
//
template<typename RPC_SERVICE, typename REQ, typename RESP>
using UnaryRequestFunc = void (RPC_SERVICE::AsyncService::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncResponseWriter<RESP>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<typename RPC_SERVICE, typename REQ, typename RESP>
using ServerStreamRequestFunc = void (RPC_SERVICE::AsyncService::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncWriter<RESP>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<typename RPC_SERVICE, typename REQ, typename RESP>
using ClientStreamRequestFunc = void (RPC_SERVICE::AsyncService::*)(::grpc::ServerContext*,
        ::grpc::ServerAsyncReader<RESP, REQ>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

//
// Template class to handle unary respone
//
template<typename RPC_SERVICE, typename REQ, typename RESP>
struct UnaryRequestContext : public RequestContext
{
    UnaryRequestContext(GrpcService<RPC_SERVICE>* service_,
                        UnaryRequestFunc<RPC_SERVICE, REQ, RESP> requestFunc_,
                        UnaryProcessFunc<RPC_SERVICE, REQ, RESP> processFunc_,
                        const void* processParam_)
        : service(service_), requestFunc(requestFunc_), processFunc(processFunc_), processParam(processParam_) {}

    UnaryRequestContext(const UnaryRequestContext& req)
        : service(req.service), requestFunc(req.requestFunc), processFunc(req.processFunc), processParam(req.processParam) {}

    virtual ~UnaryRequestContext() = default;

    GrpcService<RPC_SERVICE>* service{nullptr};

    // Pointer to function that *request* the system to start processing given requests
    UnaryRequestFunc<RPC_SERVICE, REQ, RESP> requestFunc{nullptr};

    // Pointer to function that does actual processing
    UnaryProcessFunc<RPC_SERVICE, REQ, RESP> processFunc{nullptr};

    // Any application-level data assigned by AddRpcRequest.
    const void* processParam{nullptr};

    REQ req;
    std::unique_ptr<::grpc::ServerAsyncResponseWriter<RESP>> resp_writer;
    std::unique_ptr<Context> ctx;

    void StartProcessing(::grpc::ServerCompletionQueue* cq) override
    {
        state = RequestContext::REQUEST;
        ctx.reset(new Context(processParam));
        resp_writer.reset(new ::grpc::ServerAsyncResponseWriter<RESP>(ctx.get()));
        req.Clear();

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        (service->async.*requestFunc)(ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process() override
    {
        // The actual processing
        RESP resp;
        (service->*processFunc)(*ctx, req, resp);

        // And we are done!
        // Let the gRPC runtime know we've finished, using the memory address 
        // of this instance as the uniquely identifying tag for the event.
        state = RequestContext::FINISH;

        resp_writer->Finish(resp, ctx->GetStatus(), this);
    }

    void EndProcessing(::grpc::ServerCompletionQueue* cq, bool isError) override
    {
        if(isError)
        {
            // TODO: Handle processing errors ...
            if(state != RequestContext::FINISH)
            {
//                ::grpc::Status grpcStatus(grpc::StatusCode::INTERNAL, "Server Completion Queue failed to read event");
//                resp_writer->FinishWithError(grpcStatus, this);
            }
        }

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone() override
    {
        auto reqCtx = new (std::nothrow) UnaryRequestContext<RPC_SERVICE, REQ, RESP>(*this);
        if(!reqCtx)
            service->srv->OnError("Clone() out of memory allocating UnaryRequestContext");
        return reqCtx;
    }

    std::string_view GetRequestName() const override { return req.GetTypeName(); }
};

//
// Template class to handle streaming respone
//
template<typename RPC_SERVICE, typename REQ, typename RESP>
struct ServerStreamRequestContext : public RequestContext
{
    ServerStreamRequestContext(GrpcService<RPC_SERVICE>* service_,
                               ServerStreamRequestFunc<RPC_SERVICE, REQ, RESP> requestFunc_,
                               ServerStreamProcessFunc<RPC_SERVICE, REQ, RESP> processFunc_,
                               const void* processParam_)
        : service(service_), requestFunc(requestFunc_), processFunc(processFunc_), processParam(processParam_) {}

    ServerStreamRequestContext(const ServerStreamRequestContext& req)
        : service(req.service), requestFunc(req.requestFunc), processFunc(req.processFunc), processParam(req.processParam) {}

    ~ServerStreamRequestContext() = default;

    GrpcService<RPC_SERVICE>* service{nullptr};

    // Pointer to function that *request* the system to start processing given requests
    ServerStreamRequestFunc<RPC_SERVICE, REQ, RESP> requestFunc{nullptr};

    // Pointer to function that does actual processing
    ServerStreamProcessFunc<RPC_SERVICE, REQ, RESP> processFunc{nullptr};

    // Any application-level data assigned by AddRpcRequest.
    const void* processParam{nullptr};

    REQ req;
    std::unique_ptr<::grpc::ServerAsyncWriter<RESP>> resp_writer;
    std::unique_ptr<ServerStreamContext> ctx;

    void StartProcessing(::grpc::ServerCompletionQueue* cq) override
    {
        state = RequestContext::REQUEST;
        ctx.reset(new ServerStreamContext(processParam));
        resp_writer.reset(new ::grpc::ServerAsyncWriter<RESP>(ctx.get()));
        req.Clear();

//        // victor test
//        TRACE("Calling requestFunc(), tag=" << this << ", state=" << GetStateStr());

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        (service->async.*requestFunc)(ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process() override
    {
        if(state == RequestContext::REQUEST)
        {
            // This is very first Process call for the given request.
            state = RequestContext::WRITE;
        }

        // The actual processing
        RESP resp;
        (service->*processFunc)(*ctx, req, resp);

        // Are there more responses to stream?
        if(ctx->streamHasMore)
        {
            // victor test
//            TRACE("Calling Write(), tag=" << this << ", state=" << GetStateStr());

            resp_writer->Write(resp, this);
        }
        // There are no more responses to stream
        else
        {
            // And we are done!
            // Let the gRPC runtime know we've finished, using the memory address 
            // of this instance as the uniquely identifying tag for the event.
            state = RequestContext::FINISH;

//            // victor test
//            TRACE("Calling Finish(), tag=" << this << ", state=" << GetStateStr());

            resp_writer->Finish(ctx->GetStatus(), this);
        }
    }

    void EndProcessing(::grpc::ServerCompletionQueue* cq, bool isError) override
    {
        if(isError)
        {
//            const char* stateStr =
//                (state == RequestContext::REQUEST ? "REQUEST (ServerAsyncWriter::requestFunc() failed)" :
//                 state == RequestContext::WRITE   ? "WRITE (ServerAsyncWriter::Write() failed)" :
//                 state == RequestContext::FINISH  ? "FINISH (ServerAsyncWriter::Finish() failed)" : "UNKNOWN");
//
            std::stringstream ss;
            ss << __func__ << ':' << __LINE__ << ' '
               << "Server streaming failed for tag=" << this << ", req=" << GetRequestName()
               << ", streamParam=" << ctx->streamParam << ", state=" << GetStateStr();
            service->srv->OnError(ss.str());

            // TODO: Handle processing errors ...
            if(state != RequestContext::FINISH)
            {
//                ::grpc::Status grpcStatus(grpc::StatusCode::INTERNAL, "Server Completion Queue failed to read event");
//                resp_writer->Finish(grpcStatus, this);
            }
        }

        if(ctx)
        {
            // End processing
            ctx->streamStatus = (isError ? StreamStatus::ERROR : StreamStatus::SUCCESS);
            RESP respDummy;
            (service->*processFunc)(*ctx, req, respDummy);
        }
        else
        {
            // Note: stream_ctx is set by a very first Process() call, that is called
            // after successful compleation of the event placed by requestFunc().
            // If processing of requestFunc() event failed, then we will be here
            // even before stream_ctx gets a chance to be initialized.
            // In this case we don't have a stream yet.
            std::stringstream ss;
            ss << __func__ << ':' << __LINE__ << ' '
               << "Ending streaming for tag=" << this << ", req=" << GetRequestName()
               << ", stream='Not Started', state=" << GetStateStr();
            service->srv->OnError(ss.str());
        }

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone() override
    {
        auto reqCtx = new (std::nothrow) ServerStreamRequestContext<RPC_SERVICE, REQ, RESP>(*this);
        if(!reqCtx)
            service->srv->OnError("Clone() out of memory allocating ServerStreamRequestContext");
        return reqCtx;
    }

    std::string_view GetRequestName() const override { return req.GetTypeName(); }
};

//
// Template class to handle client streaming respone
//
template<typename RPC_SERVICE, typename REQ, typename RESP>
struct ClientStreamRequestContext : public RequestContext
{
    ClientStreamRequestContext(GrpcService<RPC_SERVICE>* service_,
                               ClientStreamRequestFunc<RPC_SERVICE, REQ, RESP> requestFunc_,
                               ClientStreamProcessFunc<RPC_SERVICE, REQ, RESP> processFunc_,
                               const void* processParam_)
        : service(service_), requestFunc(requestFunc_), processFunc(processFunc_), processParam(processParam_) {}

    ClientStreamRequestContext(const ClientStreamRequestContext& req)
        : service(req.service), requestFunc(req.requestFunc), processFunc(req.processFunc), processParam(req.processParam) {}

    ~ClientStreamRequestContext() = default;

    GrpcService<RPC_SERVICE>* service{nullptr};

    // Pointer to function that *request* the system to start processing given requests
    ClientStreamRequestFunc<RPC_SERVICE, REQ, RESP> requestFunc{nullptr};

    // Pointer to function that does actual processing
    ClientStreamProcessFunc<RPC_SERVICE, REQ, RESP> processFunc{nullptr};

    // Any application-level data assigned by AddRpcRequest.
    const void* processParam{nullptr};

    REQ req;
    RESP resp;
    std::unique_ptr<::grpc::ServerAsyncReader<RESP, REQ>> req_reader;
    std::unique_ptr<ClientStreamContext> ctx;

    void StartProcessing(::grpc::ServerCompletionQueue* cq) override
    {
        state = RequestContext::REQUEST;
        ctx.reset(new ClientStreamContext(processParam));
        req_reader.reset(new ::grpc::ServerAsyncReader<RESP, REQ>(ctx.get()));

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        (service->async.*requestFunc)(ctx.get(), req_reader.get(), cq, cq, this);
    }

    void Process() override
    {
        if(state == RequestContext::REQUEST)
        {
            // This is very first Process call for the given request.
            ctx->streamHasMore = true;

            // Start reading
            //TRACE("this=" << this << ", ASK TO READ");  // victor test

            state = RequestContext::READ;
            req.Clear();
            req_reader->Read(&req, this);
        }
        else if(state == RequestContext::READ)
        {
            //TRACE("this=" << this << ", READ COMPLETE");    // victor test

            (service->*processFunc)(*ctx, req, resp);

            // Is processing failed?
            if(!ctx->GetStatus().ok())
            {
                //TRACE("this=" << this << ", Processing returned error " << ctx->GetStatus().error_code());    // victor test

                // Processing returned error
                state = RequestContext::FINISH;
                req_reader->FinishWithError(ctx->GetStatus(), this);
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
            ctx->streamHasMore = false;
            (service->*processFunc)(*ctx, req, resp);

            // Let the gRPC runtime know we've finished, using the
            // memory address of this instance as the uniquely identifying tag for
            // the event.
            req_reader->Finish(resp, ctx->GetStatus(), this);
        }
        else
        {
            //ERRORMSG("Invalid ClientStreamRequestContext::Process state() " << state); // victor test

//            // TODO - handle errors
//            serv->OnError("Invalid ClientStreamRequestContext::Process state() " + std::to_string(state));
        }
    }

    void EndProcessing(::grpc::ServerCompletionQueue* cq, bool isError) override
    {
        if(isError)
        {
            // TODO: Handle processing errors ...
        }

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone() override
    {
        auto reqCtx = new (std::nothrow) ClientStreamRequestContext<RPC_SERVICE, REQ, RESP>(*this);
        if(!reqCtx)
            service->srv->OnError("Clone() out of memory allocating ClientStreamRequestContext");
        return reqCtx;
    }

    std::string_view GetRequestName() const override { return req.GetTypeName(); }
};

//
// Template implementation of service-specific GrpcService class
//
template<typename RPC_SERVICE>
class GrpcService : public GrpcServiceBase
{
public:
    GrpcService() = default;
    virtual ~GrpcService() = default;

    // Note: The
    // RPC_SERVICE::service_full_name() method is not well-documented,
    // but is automatically generated for each service class. This method
    // may require replacement if it is no longer generated by the gRPC tooling.
    static constexpr char const* service_full_name() { return RPC_SERVICE::service_full_name(); }

    const char* GetName() override { return service_full_name(); }

    // Get the actual AsyncService
    virtual ::grpc::Service* GetService() override { return &async; }

    // Add request for unary RPC
    template<typename REQ, typename RESP, typename SERVICE_IMPL, typename REQUEST_FUNC>
    void Bind(void (SERVICE_IMPL::*processFunc)(const Context&, const REQ&, RESP&),
              REQUEST_FUNC requestFunc, const void* processParam = nullptr)
    {
        // Bind RPC-specific grpc service with the corresponding processing function.
        auto ctx = new (std::nothrow) UnaryRequestContext<RPC_SERVICE, REQ, RESP>(
            this, requestFunc, (UnaryProcessFunc<RPC_SERVICE, REQ, RESP>)processFunc, processParam);
        if(ctx)
            srv->AddRpcRequest(ctx);
        else
            srv->OnError("Bind() out of memory allocating UnaryRequestContext");
    }

    // Add request for server-stream RPC
    template<typename REQ, typename RESP, typename SERVICE_IMPL, typename REQUEST_FUNC>
    void Bind(void (SERVICE_IMPL::*processFunc)(const ServerStreamContext&, const REQ&, RESP&),
              REQUEST_FUNC requestFunc, const void* processParam = nullptr)
    {
        // Bind RPC-specific grpc service with the corresponding processing function.
        auto ctx = new (std::nothrow) ServerStreamRequestContext<RPC_SERVICE, REQ, RESP>
            (this, requestFunc, (ServerStreamProcessFunc<RPC_SERVICE, REQ, RESP>)processFunc, processParam);
        if(ctx)
            srv->AddRpcRequest(ctx);
        else
            srv->OnError("Bind() out of memory allocating ServerStreamRequestContext");
    }

    // Add request for client-stream RPC
    template<typename REQ, typename RESP, typename SERVICE_IMPL, typename REQUEST_FUNC>
    void Bind(void (SERVICE_IMPL::*processFunc)(const ClientStreamContext&, const REQ&, RESP&),
              REQUEST_FUNC requestFunc, const void* processParam = nullptr)
    {
        // Bind RPC-specific grpc service with the corresponding processing function.
        auto ctx = new (std::nothrow) ClientStreamRequestContext<RPC_SERVICE, REQ, RESP>
            (this, requestFunc, (ClientStreamProcessFunc<RPC_SERVICE, REQ, RESP>)processFunc, processParam);
        if(ctx)
            srv->AddRpcRequest(ctx);
        else
            srv->OnError("Bind() out of memory allocating ClientStreamRequestContext");
    }

protected:
    typename RPC_SERVICE::AsyncService async;
    GrpcServer* srv{nullptr};

    friend class GrpcServer;

    template<typename RPC_SERVICE_, typename REQ, typename RESP>
    friend struct UnaryRequestContext;

    template<typename RPC_SERVICE_, typename REQ, typename RESP>
    friend struct ServerStreamRequestContext;

    template<typename RPC_SERVICE_, typename REQ, typename RESP>
    friend struct ClientStreamRequestContext;
};

} //namespace gen


#endif // __GRPC_SERVER_HPP__
// *INDENT-ON*


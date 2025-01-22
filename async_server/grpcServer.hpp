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
#include "grpcUtils.hpp"    // FormatDnsAddressUri
#include <sstream>          // stringstream
#include <thread>           // std::thread
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

    virtual void Process() = 0;
    virtual void StartProcessing(::grpc::ServerCompletionQueue* cq) = 0;
    virtual void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError) = 0;

    virtual RequestContext* Clone() = 0;
    virtual std::string GetRequestName() const = 0;
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

    void Shutdown() { runService = false; }
    bool IsRunning() { return runService; }

    template<class GRPC_SERVICE, class... SERVICE_ARGS>
    GRPC_SERVICE* AddService(SERVICE_ARGS&&...args)
    {
        // Note: Use std::forward<Args>(args) to ensures that rvalue references are preserved correctly
        GRPC_SERVICE* service = new (std::nothrow) GRPC_SERVICE(std::forward<SERVICE_ARGS>(args)...);
        if(!service)
        {
            OnError("AddService() out of memory allocating GRPC_SERVICE");
            return nullptr;
        }
        else if(service->srv = this; !service->OnInit())
        {
            OnError("OnInit() failed for service '" + std::string(service->GetName()) + "'");
            delete service;
            return nullptr;
        }

        serviceMap[service->GetName()].reset(service);
        return service;
    }

    ::grpc::Service* GetService(const std::string& serviceName)
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
                ::grpc::Service* service = pair.second.get();
                builder.RegisterService(service);
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
                    ss << "Server Completion Queue failed to read event for tag=" << tag << ", "
                       << "req=" << ctx->GetRequestName();
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
                ss << "Unknown Completion Queue event: ctx->state=" << ctx->state << ", tag=" << tag << ", "
                   << "req=" << ctx->GetRequestName();
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
    virtual void OnRun() {}

    // Class data
    std::map<std::string, std::unique_ptr<::grpc::Service>> serviceMap;
    std::list<std::unique_ptr<RequestContext>> requestContextList;
    std::atomic<bool> runService{false};            // Initially, since we are not running yet
    std::atomic<bool> runThreads{false};            // Initially, since we don't have any threads yet
    unsigned int runIntervalMicroseconds{1000000};  // 1 secs default

    template<class RPC_SERVICE>
    friend class GrpcService;
};

template<class RPC_SERVICE>
class GrpcService;

//
// Template pointer to function that does actual unary/stream processing
//
template<class RPC_SERVICE, class REQ, class RESP>
using UnaryProcessFunc = void (GrpcService<RPC_SERVICE>::*)(const RpcContext&, const REQ&, RESP&);

template<class RPC_SERVICE, class REQ, class RESP>
using ServerStreamProcessFunc = void (GrpcService<RPC_SERVICE>::*)(const RpcServerStreamContext&, const REQ&, RESP&);

template<class RPC_SERVICE, class REQ, class RESP>
using ClientStreamProcessFunc = void (GrpcService<RPC_SERVICE>::*)(const RpcClientStreamContext&, const REQ&, RESP&);

//
// Template pointer to function that *request* the system to start processing unary/strean requests
//
template<class RPC_SERVICE, class REQ, class RESP>
using UnaryRequestFunc = void (RPC_SERVICE::AsyncService::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncResponseWriter<RESP>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<class RPC_SERVICE, class REQ, class RESP>
using ServerStreamRequestFunc = void (RPC_SERVICE::AsyncService::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncWriter<RESP>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<class RPC_SERVICE, class REQ, class RESP>
using ClientStreamRequestFunc = void (RPC_SERVICE::AsyncService::*)(::grpc::ServerContext*,
        ::grpc::ServerAsyncReader<RESP, REQ>*,
        ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

//
// Template class to handle unary respone
//
template<class RPC_SERVICE, class REQ, class RESP>
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

    void StartProcessing(::grpc::ServerCompletionQueue* cq) override
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
        (service->*requestFunc)(srv_ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process() override
    {
        // The actual processing
        RpcContext rpc_ctx(srv_ctx.get(), processParam);
        RESP resp;
        (service->*processFunc)(rpc_ctx, req, resp);

        // And we are done!
        ::grpc::Status grpcStatus(rpc_ctx.GetStatus(), rpc_ctx.GetError());

        // Let the gRPC runtime know we've finished, using the
        // memory address of this instance as the uniquely identifying tag for
        // the event.
        state = RequestContext::FINISH;

        resp_writer->Finish(resp, grpcStatus, this);
    }

    void EndProcessing(const GrpcServer* /*serv*/, ::grpc::ServerCompletionQueue* cq, bool /*isError*/) override
    {
        // TODO
        // Handle processing errors ...

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone() override
    {
        auto ctx = new (std::nothrow) UnaryRequestContext<RPC_SERVICE, REQ, RESP>(*this);
        if(!ctx)
            service->srv->OnError("Clone() out of memory allocating UnaryRequestContext");
        return ctx;
    }

    std::string GetRequestName() const override { return req.GetTypeName(); }
};

//
// Template class to handle streaming respone
//
template<class RPC_SERVICE, class REQ, class RESP>
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
    std::unique_ptr<RpcServerStreamContext> stream_ctx;

    void StartProcessing(::grpc::ServerCompletionQueue* cq) override
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
        (service->*requestFunc)(srv_ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process() override
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
        (service->*processFunc)(*stream_ctx, req, resp);

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

    void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError) override
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
                ss << "Error streaming for tag=" << this << ", req=" << GetRequestName() << ", "
                   << "streamParam=" << stream_ctx->streamParam << ", state=" << stateStr;
                serv->OnError(ss.str());
            }

            // End processing
            stream_ctx->streamStatus = (isError ? StreamStatus::ERROR : StreamStatus::SUCCESS);
            RESP respDummy;
            (service->*processFunc)(*stream_ctx, req, respDummy);
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
            ss << "Ending streaming for tag=" << this << ", req=" << GetRequestName() << ", "
               << "stream='Not Started', state=" << stateStr;
            serv->OnError(ss.str());
        }

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone() override
    {
        auto ctx = new (std::nothrow) ServerStreamRequestContext<RPC_SERVICE, REQ, RESP>(*this);
        if(!ctx)
            service->srv->OnError("Clone() out of memory allocating ServerStreamRequestContext");
        return ctx;
    }

    std::string GetRequestName() const override { return req.GetTypeName(); }
};

//
// Template class to handle client streaming respone
//
template<class RPC_SERVICE, class REQ, class RESP>
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
    std::unique_ptr<RpcClientStreamContext> stream_ctx;

    void StartProcessing(::grpc::ServerCompletionQueue* cq) override
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
        (service->*requestFunc)(srv_ctx.get(), req_reader.get(), cq, cq, this);
    }

    void Process() override
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

            (service->*processFunc)(*stream_ctx, req, resp);

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
            (service->*processFunc)(*stream_ctx, req, resp);

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

    void EndProcessing(const GrpcServer* serv, ::grpc::ServerCompletionQueue* cq, bool isError) override
    {
        //TRACE("Done");  // victor test

        // Ask the system start processing requests
        StartProcessing(cq);
    }

    virtual RequestContext* Clone() override
    {
        auto ctx = new (std::nothrow) ClientStreamRequestContext<RPC_SERVICE, REQ, RESP>(*this);
        if(!ctx)
            service->srv->OnError("Clone() out of memory allocating ClientStreamRequestContext");
        return ctx;
    }

    std::string GetRequestName() const override { return req.GetTypeName(); }
};

//
// Template implementation of service-specific GrpcService class
//
template<class RPC_SERVICE>
class GrpcService : public RPC_SERVICE::AsyncService
{
public:
    GrpcService() = default;
    virtual ~GrpcService() = default;

    // Note: service_full_name() is not well documented, but
    // is generated for every service class. It might need to
    // be replaced if/when it's no longer generated.
    const char* GetName() { return RPC_SERVICE::service_full_name(); }

    // For derived class to override
    virtual bool OnInit() = 0;

    // Add request for unary RPC
    template<class REQ, class RESP, class SERVICE_IMPL>
    void Bind(void (SERVICE_IMPL::*processFunc)(const RpcContext&, const REQ&, RESP&),
              auto requestFunc, const void* processParam = nullptr)
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
    template<class REQ, class RESP, class SERVICE_IMPL>
    void Bind(void (SERVICE_IMPL::*processFunc)(const RpcServerStreamContext&, const REQ&, RESP&),
              auto requestFunc, const void* processParam = nullptr)
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
    template<class REQ, class RESP, class SERVICE_IMPL>
    void Bind(void (SERVICE_IMPL::*processFunc)(const RpcClientStreamContext&, const REQ&, RESP&),
              auto requestFunc, const void* processParam = nullptr)
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
    GrpcServer* srv{nullptr};

    friend class GrpcServer;

    template<class RPC_SERVICE_, class REQ, class RESP>
    friend struct UnaryRequestContext;

    template<class RPC_SERVICE_, class REQ, class RESP>
    friend struct ServerStreamRequestContext;

    template<class RPC_SERVICE_, class REQ, class RESP>
    friend struct ClientStreamRequestContext;
};

} //namespace gen


#endif // __GRPC_SERVER_HPP__
// *INDENT-ON*


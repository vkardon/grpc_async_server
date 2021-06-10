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
#include <grpc++/grpc++.h>
#pragma GCC diagnostic pop

#include "grpcContext.hpp"  // RpcContext
#include <sstream>          // stringstream

namespace gen {

//const int GRPC_MAX_MESSAGE_SIZE = 1024 * 8; // 8K

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

    virtual RequestContext* CloneMe() const = 0;
};

// Note: the only reason we use ServiceWrapper is because grpc::Service
// is not fully declared when we delete all services from serviceList_
// at the end of Run(). Wrapping it into this helper class solves the issue.
struct ServiceWrapper
{
    virtual ~ServiceWrapper() = default;
    virtual ::grpc::Service* get() = 0;
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
        REQ*, ::grpc::ServerAsyncResponseWriter<RESP>*, ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<class RPC_SERVICE, class REQ, class RESP>
using ServerStreamRequestFuncPtr = void (AsyncService<RPC_SERVICE>::*)(::grpc::ServerContext*,
        REQ*, ::grpc::ServerAsyncWriter<RESP>*, ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

template<class RPC_SERVICE, class REQ, class RESP>
using ClientStreamRequestFuncPtr = void (AsyncService<RPC_SERVICE>::*)(::grpc::ServerContext*,
        ::grpc::ServerAsyncReader<RESP, REQ>*, ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);

//
// Class GrpcServer
//
class GrpcServer
{
    // Helper class to instantiate RPC-specific service
    template<class RPC_SERVICE>
    struct ServiceWrapperImpl : public ServiceWrapper, public RPC_SERVICE::AsyncService
    {
        //virtual ~ServiceWrapperImpl() { std::cout << ">>> " << __func__ << ": this=" << this << std::endl; }
        virtual ~ServiceWrapperImpl() = default;
        ::grpc::Service* get() { return this; } // Because AsyncService is derived from Service
    };

public:
    GrpcServer() = default;
    virtual ~GrpcServer() = default;

    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;

    // Run() is blocked. It doesn't return until OnRun() returns false.
    void Run(unsigned short port, int threadCount);
    void Run(const char* domainSocketPath, int threadCount);
    void Run(const std::vector<std::string>& addressUriArr, int threadCount);

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

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& /*err*/) const {}
    virtual void OnInfo(const std::string& /*info*/) const {}

private:
    void RunImpl(const std::vector<std::string>& addressUriArr, int threadCount);
    void ProcessEvents(::grpc::ServerCompletionQueue* cq, int threadIndex);

    // Helpers
    template<class RPC_SERVICE>
    void AddService(GrpcService* grpcService);

    // For derived class to override
    virtual bool OnInit() = 0;
    virtual bool OnRun() = 0;

    // Class data
    int contextCount_ = 0;
    std::list<GrpcService*> serviceList_;
    std::list<RequestContext*> requestContextList_;
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
    GrpcService* grpcService = nullptr;

    // Any application-level data assigned by AddRpcRequest.
    // It will be make available to RPC function through RpcContext.
    const void* processParam = nullptr; // Any application-level data stored by AddRpcRequest

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
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service->get();
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
    GrpcService* grpcService = nullptr;

    // Any application-level data assigned by AddRpcRequest.
    // It will be make available to RPC function through RpcContext.
    const void* processParam = nullptr; // Any application-level data stored by AddRpcRequest

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
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service->get();
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
        ctx->grpcService  = grpcService;
        ctx->processParam = processParam;
        ctx->processFunc  = processFunc;
        ctx->requestFunc  = requestFunc;
        return ctx;
    }

    REQ req;
    RESP resp;
    std::unique_ptr<::grpc::ServerAsyncReader<RESP, REQ>> req_reader;
    std::unique_ptr<RpcClientStreamContext> stream_ctx;
    GrpcService* grpcService = nullptr;

    // Any application-level data assigned by AddRpcRequest.
    // It will be make available to RPC function through RpcContext.
    const void* processParam = nullptr; // Any application-level data stored by AddRpcRequest

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
        AsyncService<RPC_SERVICE>* asyncService = (AsyncService<RPC_SERVICE>*)grpcService->service->get();
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
        grpcService->service = new (std::nothrow) ServiceWrapperImpl<RPC_SERVICE>;
        grpcService->serviceName = RPC_SERVICE::service_full_name();
        serviceList_.push_back(grpcService);
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
    for(int i = 0; i < contextCount_; i++)
    {
        requestContextList_.push_back(ctx.CloneMe());
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
    ctx.grpcService  = grpcService;
    ctx.processParam = processParam;
    ctx.processFunc  = processFunc;
    ctx.requestFunc  = requestFunc;

    // Add request contexts
    for(int i = 0; i < contextCount_; i++)
    {
        requestContextList_.push_back(ctx.CloneMe());
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
    ctx.grpcService  = grpcService;
    ctx.processParam = processParam;
    ctx.processFunc  = processFunc;
    ctx.requestFunc  = requestFunc;

    // Add request contexts
    for(int i = 0; i < contextCount_; i++)
    {
        requestContextList_.push_back(ctx.CloneMe());
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


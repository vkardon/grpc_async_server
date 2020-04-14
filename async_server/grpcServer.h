//
// grpcServer.h
//
#ifndef __GRPC_SERVER_H__
#define __GRPC_SERVER_H__

// Note: The multithreaded gRpc server is implemented Based on
//       grpc_1.0.0/test/cpp/end2end/thread_stress_test.cc
//

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <grpc++/grpc++.h>
#pragma GCC diagnostic pop

#include "grpcContext.h"  // RpcContext
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
    enum : char { UNKNOWN=0, REQUEST, WRITE, FINISH } state = UNKNOWN;

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

    // Create RPC-specific grpc service (if not created yet) and
    // bind it with the corresponding processing function.
    template <class RPC_SERVICE>
    void AddRpcRequest(GrpcService* grpcService, const RequestContext& ctx)
    {
        // Create new service if not created yet
        if(grpcService->service == nullptr)
        {
            grpcService->service = new (std::nothrow) ServiceWrapperImpl<RPC_SERVICE>;
            serviceList_.push_back(grpcService);
            //std::cout << ">>> " << __func__ << ": service=" << grpcService->service << std::endl;
        }

        // Add request contexts
        for(int i = 0; i < contextCount_; i++)
        {
            requestContextList_.push_back(ctx.CloneMe());
        }
    }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& /*err*/) const {}
    virtual void OnInfo(const std::string& /*info*/) const {}

private:
    void RunImpl(unsigned short port, int threadCount);
    void ProcessRpcsProc(::grpc::ServerCompletionQueue* cq, int threadIndex);

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
struct RequestContextUnary : public RequestContext
{
    RequestContextUnary() = default;
    virtual ~RequestContextUnary() = default;

    virtual RequestContext* CloneMe() const
    {
        RequestContextUnary* ctx = new (std::nothrow) RequestContextUnary;
        ctx->procService = procService;
        ctx->fProcessPtr = fProcessPtr;
        ctx->fRequestPtr = fRequestPtr;
        return ctx;
    }

    REQ req;
    std::unique_ptr<::grpc::ServerAsyncResponseWriter<RESP>> resp_writer;
    GrpcService* procService = nullptr;

    // Define the service type
    typedef typename RPC_SERVICE::AsyncService AsyncServiceType;

    // Pointer to function that does actual processing
    typedef void (GrpcService::*fProcessPtrType)(const RpcContext&, const REQ&, RESP&);
    fProcessPtrType fProcessPtr = nullptr;

    // Pointer to function that *request* the system to start processing given requests
    typedef void (AsyncServiceType::*fRequestPtrType)(::grpc::ServerContext*, REQ*,
            ::grpc::ServerAsyncResponseWriter<RESP>*, ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);
    fRequestPtrType fRequestPtr = nullptr;

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
        AsyncServiceType* asyncService = (AsyncServiceType*)procService->service->get();
        (asyncService->*fRequestPtr)(srv_ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process()
    {
        // The actual processing
        RpcContext rpc_ctx(srv_ctx.get());
        RESP resp;
        (procService->*fProcessPtr)(rpc_ctx, req, resp);

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
struct RequestContextStream : public RequestContext
{
    RequestContextStream() = default;
    ~RequestContextStream() = default;

    virtual RequestContext* CloneMe() const
    {
        RequestContextStream* ctx = new (std::nothrow) RequestContextStream;
        ctx->procService = procService;
        ctx->fProcessPtr = fProcessPtr;
        ctx->fRequestPtr = fRequestPtr;
        return ctx;
    }

    REQ req;
    std::unique_ptr<::grpc::ServerAsyncWriter<RESP>> resp_writer;
    std::unique_ptr<RpcStreamContext> stream_ctx;
    GrpcService* procService = nullptr;

    // Pointer to function that does actual processing
    typedef void (GrpcService::*fProcessPtrType)(const RpcStreamContext&, const REQ&, RESP&);
    fProcessPtrType fProcessPtr = nullptr;

    // Define the service type
    typedef typename RPC_SERVICE::AsyncService AsyncServiceType;

    // Pointer to function that *request* the system to start processing given requests
    typedef void (AsyncServiceType::*fRequestPtrType)(::grpc::ServerContext*, REQ*,
            ::grpc::ServerAsyncWriter<RESP>*, ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*, void*);
    fRequestPtrType fRequestPtr = nullptr;

    void StartProcessing(::grpc::ServerCompletionQueue* cq)
    {
        state = RequestContext::REQUEST;
        srv_ctx.reset(new ::grpc::ServerContext);
        resp_writer.reset(new ::grpc::ServerAsyncWriter<RESP>(srv_ctx.get()));
        stream_ctx.reset();
        req.Clear();

//            // victor test
//            OUTMSG_MT(__func__ << ": Calling fRequestPtr(), tag='" << this << "', "
//                    << "state=" << (state == RequestContext::REQUEST ? "REQUEST" :
//                                    state == RequestContext::WRITE   ? "WRITE"   :
//                                    state == RequestContext::FINISH  ? "FINISH"  : "UNKNOWN"));

        // *Request* that the system start processing given requests.
        // In this request, "this" acts as the tag uniquely identifying
        // the request (so that different context instances can serve
        // different requests concurrently), in this case the memory address
        // of this context instance.
        AsyncServiceType* asyncService = (AsyncServiceType*)procService->service->get();
        (asyncService->*fRequestPtr)(srv_ctx.get(), &req, resp_writer.get(), cq, cq, this);
    }

    void Process()
    {
        if(state == RequestContext::REQUEST)
        {
            // This is very first Process call for the given request.
            state = RequestContext::WRITE;

            // Create new RpcStreamContext
            stream_ctx.reset(new RpcStreamContext(srv_ctx.get()));
        }

        // The actual processing
        RESP resp;
        (procService->*fProcessPtr)(*stream_ctx, req, resp);

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
                    (state == RequestContext::REQUEST ? "REQUEST (ServerAsyncWriter::fRequestPtr() failed)" :
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
            (procService->*fProcessPtr)(*stream_ctx, req, respDummy);
        }
        else
        {
            // Note: stream_ctx is set by a very first Process() call, that is called
            // after successful compleation of the event placed by fRequestPtr().
            // If processing of fRequestPtr() event failed, then we will be here
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

} //namespace gen

#endif // __GRPC_SERVER_H__


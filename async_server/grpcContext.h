//
// grpcContext.h
//
#ifndef __GRPC_CONTEXT_H__
#define __GRPC_CONTEXT_H__

#include <grpcpp/impl/codegen/status_code_enum.h>   // grpc::StatusCode
#include <string>

namespace grpc{ class ServerContext; }

namespace gen {

//
// Class RpcContext is sent to unary process function
//
class RpcContext
{
public:
    RpcContext(::grpc::ServerContext* ctx) : srvCtx(ctx) {}
    ~RpcContext() = default;

    void SetStatus(::grpc::StatusCode statusCode, const std::string& err) const;
    ::grpc::StatusCode GetStatus() const { return grpcStatusCode; }
    const std::string& GetError() const { return grpcErr; }

    void GetMetadata(const char* key, std::string& value) const;
    void SetMetadata(const char* key, const std::string& value) const;
    std::string Peer() const;

private:
    ::grpc::ServerContext* srvCtx = nullptr;
    mutable ::grpc::StatusCode grpcStatusCode{grpc::UNKNOWN};
    mutable std::string grpcErr;
};

//
// Class RpcStreamContext is sent to stream process function
//
enum StreamStatus : char { STREAMING=1, SUCCESS, ERROR };

class RpcStreamContext : public RpcContext
{
public:
    RpcStreamContext(::grpc::ServerContext* ctx) : RpcContext(ctx) {}
    ~RpcStreamContext() = default;

    StreamStatus GetStreamStatus() const { return streamStatus; }

    void  SetHasMore(bool hasMore) const { streamHasMore = hasMore; }
    bool  GetHasMore() const { return streamHasMore; }

    void  SetParam(void* param) const { streamParam = param; }
    void* GetParam() const { return streamParam; }

private:
    StreamStatus streamStatus = STREAMING;
    mutable bool streamHasMore = false;   // Are there more responses to stream?
    mutable void* streamParam = nullptr;  // Request-specific stream data (for derived class to use)

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct RequestContextStream;
};

//
// This is the base class for service-specific RPC-processing classes
//
class GrpcServer;
class ServiceWrapper;

class GrpcService
{
public:
    virtual ~GrpcService() = default;
    virtual bool Init(GrpcServer* srv) = 0;
    ServiceWrapper* service = nullptr;
};


//
// Helper macros PROCESS_UNARY and PROCESS_STREAM to start processing requests.
//
// The equivalent code example is below:
//
//    std::vector<RequestContextUnary<SessionManagerService, ShutdownRequest, ShutdownResponse> > Shutdown_contexts(context_count);
//    for(RequestContextUnary<SessionManagerService, ShutdownRequest, ShutdownResponse>& ctx : Shutdown_contexts)
//    {
//        ctx.asyncService = &sessmgr_service;
//        ctx.fProcessPtr = &GrpcServer::Shutdown;
//        ctx.fRequestPtr = &SessionManagerService::AsyncService::RequestShutdown;
//        ctx.StartProcessing(this);
//    }

//
// Helper macro to create request context for the given service
//
#define ADD_REQUEST(REQUEST_CONTEXT, RPC, RPC_SERVICE, PROC_FUNC_PTR, SERVER_PTR) \
    REQUEST_CONTEXT ctx;                                                          \
    ctx.procService = this;                                                       \
    ctx.fProcessPtr = (REQUEST_CONTEXT::fProcessPtrType)PROC_FUNC_PTR;            \
    ctx.fRequestPtr = &RPC_SERVICE::AsyncService::Request##RPC;                   \
    SERVER_PTR->AddRpcRequest<RPC_SERVICE>(this, ctx);                            \

//
// Generic macro to add processing for unary RPC call
//
#define ADD_UNARY(RPC, REQ, RESP, RPC_SERVICE, PROC_FUNC_PTR, SERVER_PTR)         \
    do {                                                                          \
        typedef gen::RequestContextUnary<RPC_SERVICE, REQ, RESP> RequestContext;  \
        ADD_REQUEST(RequestContext, RPC, RPC_SERVICE, PROC_FUNC_PTR, SERVER_PTR)  \
    } while(false);                                                               \

//
// Generic macro to add processing for streaming RPC call
//
#define ADD_STREAM(RPC, REQ, RESP, RPC_SERVICE, PROC_FUNC_PTR, SERVER_PTR)        \
    do {                                                                          \
        typedef gen::RequestContextStream<RPC_SERVICE, REQ, RESP> RequestContext; \
        ADD_REQUEST(RequestContext, RPC, RPC_SERVICE, PROC_FUNC_PTR, SERVER_PTR)  \
    } while(false);                                                               \


} //namespace gen


#endif // __GRPC_CONTEXT_H__


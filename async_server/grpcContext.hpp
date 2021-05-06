// *INDENT-OFF*
//
// grpcContext.hpp
//
#ifndef __GRPC_CONTEXT_HPP__
#define __GRPC_CONTEXT_HPP__

#include <grpcpp/impl/codegen/status_code_enum.h>   // grpc::StatusCode
#include <string>

namespace grpc { class ServerContext; }

namespace gen {

//
// Class RpcContext is sent to unary process function
//
class RpcContext
{
public:
    RpcContext(::grpc::ServerContext* ctx, const void* param) : srvCtx(ctx), rpcParam(param) {}
    ~RpcContext() = default;

    void SetStatus(::grpc::StatusCode statusCode, const std::string& err) const;
    ::grpc::StatusCode GetStatus() const { return grpcStatusCode; }
    const std::string& GetError() const { return grpcErr; }

    void GetMetadata(const char* key, std::string& value) const;
    void SetMetadata(const char* key, const std::string& value) const;
    std::string Peer() const;

    // Get application-level data set by AddUnaryRpcRequest/AddStreamRpcRequest
    const void* GetRpcParam() const { return rpcParam; }

private:
    ::grpc::ServerContext* srvCtx = nullptr;
    const void* rpcParam{nullptr};
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
    RpcStreamContext(::grpc::ServerContext* ctx, const void* param) : RpcContext(ctx, param) {}
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
    friend struct StreamRequestContext;
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


} //namespace gen

#endif // __GRPC_CONTEXT_HPP__
// *INDENT-ON*


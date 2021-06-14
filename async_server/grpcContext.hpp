// *INDENT-OFF*
//
// grpcContext.hpp
//
#ifndef __GRPC_CONTEXT_HPP__
#define __GRPC_CONTEXT_HPP__

#include <grpcpp/impl/codegen/status_code_enum.h>   // grpc::StatusCode
#include <string>

namespace grpc { class ServerContext; }
namespace grpc { class Service; }

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
// Class RpcServerStreamContext is sent to stream process function
//
enum StreamStatus : char { STREAMING=1, SUCCESS, ERROR };

class RpcServerStreamContext : public RpcContext
{
public:
    RpcServerStreamContext(::grpc::ServerContext* ctx, const void* param) : RpcContext(ctx, param) {}
    ~RpcServerStreamContext() = default;

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
    friend struct ServerStreamRequestContext;
};

//
// Class RpcClientStreamContext is sent to client stream process function
//
class RpcClientStreamContext : public RpcContext
{
public:
    RpcClientStreamContext(::grpc::ServerContext* ctx, const void* param) : RpcContext(ctx, param) {}
    ~RpcClientStreamContext() = default;

    bool GetHasMore() const { return streamHasMore; }

    void  SetParam(void* param) const { streamParam = param; }
    void* GetParam() const { return streamParam; }

private:
    bool streamHasMore = false;           // Are there more request to read?
    mutable void* streamParam = nullptr;  // Request-specific stream data (for derived class to use)

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct ClientStreamRequestContext;
};

//
// This is the base class for service-specific RPC-processing classes
//
class GrpcServer;

class GrpcService
{
public:
    virtual ~GrpcService() = default;
    virtual bool Init(GrpcServer* srv) = 0;
    const char* GetName() { return serviceName; }

private:
    ::grpc::Service* service = nullptr;
    const char* serviceName = "";

    friend class GrpcServer;

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct UnaryRequestContext;

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct ServerStreamRequestContext;

    template<class RPC_SERVICE, class REQ, class RESP>
    friend struct ClientStreamRequestContext;
};


} //namespace gen

#endif // __GRPC_CONTEXT_HPP__
// *INDENT-ON*


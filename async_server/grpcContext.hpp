// *INDENT-OFF*
//
// gContext.hpp
//
#ifndef __GRPC_CONTEXT_HPP__
#define __GRPC_CONTEXT_HPP__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <grpcpp/impl/codegen/status_code_enum.h>   // grpc::StatusCode
#include <grpcpp/impl/codegen/server_context.h>     // grpc::ServerContext
#pragma GCC diagnostic pop

#include <string>

namespace grpc { class Service; }

namespace gen {

//
// Class Context is sent to unary process function
//
class Context : public grpc::ServerContext
{
public:
    Context(const void* param) : rpcParam(param) {}
    ~Context() = default;

    const ::grpc::Status& GetStatus() const { return grpcStatus; }

    void SetStatus(::grpc::StatusCode statusCode, const std::string& err) const
    {
        // Note: Ignore err if status is grpc::OK. Otherwise, it will be
        // an error to construct gen::Status::OK with non-empty error_message.
        if(statusCode != grpc::OK)
            grpcStatus = ::grpc::Status(statusCode, err);
        else
            grpcStatus = ::grpc::Status::OK;
    }

    void SetMetadata(const char* key, const std::string& value) const
    {
        // TODO: What would be the preferred way to change grpc::ServerContext
        // metadata instead of removing constness from its pointer?
        const_cast<Context*>(this)->grpc::ServerContext::AddTrailingMetadata(key, value);
    }

    std::string GetMetadata(const char* key) const
    {
        const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata = grpc::ServerContext::client_metadata();
        if(auto itr = metadata.find(key); itr != metadata.end())
            return std::string(itr->second.data(), itr->second.size());
        else
            return "";
    }

    std::string Peer() const
    {
        std::string peer = grpc::ServerContext::peer();

        // Note: Un-escape peer by replacing "%5B" and "%5D" with "[" and "]"
        // respectively in order to support older gRpc releases
        Replace(peer, "%5B", "[");
        Replace(peer, "%5D", "]");
        return peer;
    }

    // Get application-level data set by AddUnaryRpcRequest/AddStreamRpcRequest
    const void* GetRpcParam() const { return rpcParam; }

private:
    // Helper method to replace all occurrences of substring with another substring
    void Replace(std::string& str, const char* substr1, const char* substr2) const
    {
        if(size_t len1 = (substr1 ? strlen(substr1) : 0); len1 > 0)
        {
            size_t len2 = (substr2 ? strlen(substr2) : 0);
            for(size_t i = str.find(substr1, 0); i != std::string::npos; i = str.find(substr1, i + len2))
                str.replace(i, len1, substr2);
        }
    };

    const void* rpcParam{nullptr};
    mutable ::grpc::Status grpcStatus{::grpc::Status::OK};
};

//
// Class ServerStreamContext is sent to stream process function
//
enum StreamStatus : char { STREAMING=1, SUCCESS, ERROR };

class ServerStreamContext : public Context
{
public:
    ServerStreamContext(const void* param) : Context(param) {}
    ~ServerStreamContext() = default;

    StreamStatus GetStreamStatus() const { return streamStatus; }

    void  SetParam(void* param) const { streamParam = param; }
    void* GetParam() const { return streamParam; }

    void  EndOfStream(::grpc::StatusCode statusCode = grpc::OK, const std::string& err = "") const
    {
        Context::SetStatus(statusCode, err);
        streamHasMore = false;
    }

private:
    // Prevent from calling Context::SetStatus, force to use EndOfStream instead
    void SetStatus(::grpc::StatusCode statusCode, const std::string& err) const = delete;

    StreamStatus streamStatus = STREAMING;
    mutable bool streamHasMore = true;    // Are there more responses to stream?
    mutable void* streamParam = nullptr;  // Request-specific stream data (for derived class to use)

    template<typename RPC_SERVICE, typename REQ, typename RESP>
    friend struct ServerStreamRequestContext;
};

//
// Class ClientStreamContext is sent to client stream process function
//
class ClientStreamContext : public Context
{
public:
    ClientStreamContext(const void* param) : Context(param) {}
    ~ClientStreamContext() = default;

    bool GetHasMore() const { return streamHasMore; }

    void  SetParam(void* param) const { streamParam = param; }
    void* GetParam() const { return streamParam; }

private:
    bool streamHasMore = false;           // Are there more request to read?
    mutable void* streamParam = nullptr;  // Request-specific stream data (for derived typename to use)

    template<typename RPC_SERVICE, typename REQ, typename RESP>
    friend struct ClientStreamRequestContext;
};

} //namespace gen

#endif // __GRPC_CONTEXT_HPP__
// *INDENT-ON*


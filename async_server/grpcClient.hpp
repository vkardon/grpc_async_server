// *INDENT-OFF*
//
// grpcClient.hpp
//
#ifndef __GRPC_CLIENT_HPP__
#define __GRPC_CLIENT_HPP__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <grpcpp/grpcpp.h>
#pragma GCC diagnostic pop

#include "grpcUtils.hpp"
unsigned short getHostPort(std::string&, const char*, const char*, const char*, std::string&);

namespace gen {

// Helper functor to read server-side STREAM grpc messages
template<class RESP>
struct RespCallbackFunctor
{
    virtual ~RespCallbackFunctor() = default;
    virtual bool operator()(const RESP& resp) = 0;
};

// Helper functor to write client-side STREAM grpc messages
template<class REQ>
struct ReqCallbackFunctor
{
    virtual ~ReqCallbackFunctor() = default;
    virtual bool operator()(REQ& resp) = 0;
};

//
// Helper class to call UNARY/STREAM gRpc service
//
template <class RPC_SERVICE>
class GrpcClient
{
public:
    GrpcClient() = default;
    ~GrpcClient() = default;

    GrpcClient(const std::string& domainSocketPath,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        Init(domainSocketPath, creds);
    }

    GrpcClient(const std::string& host, unsigned short port,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        Init(host, port, creds);
    }

    // To call the server, we need to instantiate a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint specified by addressUri.
    // Note: The channel isn't authenticated by default (use of InsecureChannelCredentials()).

    bool Init(const std::string& host, unsigned short port,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        return InitFromAddressUri(FormatDnsAddressUri(host.c_str(), port), creds);
    }

    bool Init(const std::string& domainSocketPath,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        return InitFromAddressUri(FormatUnixDomainSocketAddressUri(domainSocketPath.c_str()), creds);
    }

    bool InitFromAddressUri(const std::string& addressUriIn,
                            const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr);

    // Terminate a channel (if created) and reset GrpcClient to initial state
    void Reset();

    // Thread-save UNARY gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallMT(GRPC_STUB_FUNC grpcStubFunc,
                const REQ& req, RESP& resp,
                const std::map<std::string, std::string>& metadata,
                std::string& errMsg);

    // Thread-save UNARY gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallMT(GRPC_STUB_FUNC grpcStubFunc,
                const REQ& req, RESP& resp,
                std::string& errMsg)
    {
        return CallMT(grpcStubFunc, req, resp, dummy_metadata, errMsg);
    }

    // Thread-save server-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                      const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                      const std::map<std::string, std::string>& metadata,
                      std::string& errMsg);

    // Thread-save server-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                      const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                      std::string& errMsg)
    {
        return CallStreamMT(grpcStubFunc, req, respCallback, dummy_metadata, errMsg);
    }

    // Thread-save client-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                            ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                            const std::map<std::string, std::string>& metadata,
                            std::string& errMsg);

    // Thread-save client-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                            ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                            std::string& errMsg)
    {
        return CallClientStreamMT(grpcStubFunc, reqCallback, resp, dummy_metadata, errMsg);
    }

    // Single threaded versions.
    // Note: The only difference versus multi-threaded versions is that
    // errMsg is not a function argument but a class member. The error
    // can be obtained after the call by calling GetError().

    // Single-thread UNARY gRpc (to be used in single-threaded application)
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool Call(GRPC_STUB_FUNC grpcStubFunc,
              const REQ& req, RESP& resp,
              const std::map<std::string, std::string>& metadata = dummy_metadata)
    {
        error.clear();
        return CallMT(grpcStubFunc, req, resp, metadata, error);
    }

    // Single-thread server-side STREAM gRpc (to be used in single-threaded application)
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStream(GRPC_STUB_FUNC grpcStubFunc,
                    const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                    const std::map<std::string, std::string>& metadata = dummy_metadata)
    {
        error.clear();
        return CallStreamMT(grpcStubFunc, req, respCallback, metadata, error);
    }

    // Single-thread client-side STREAM gRpc (to be used in single-threaded application)
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                          ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                          const std::map<std::string, std::string>& metadata = dummy_metadata)
    {
        error.clear();
        return CallClientStreamMT(grpcStubFunc, reqCallback, resp, metadata, error);
    }

    const std::shared_ptr<grpc::ChannelCredentials> GetCredentials() const { return creds; }
    const std::string GetAddressUri() const { return addressUri; }
    bool IsValid() const { return (bool)stub; }

    // Note: GetError() is only valid when using with single-threaded calls
    const std::string GetError() const { return error; }

private:
    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    void SetError(std::string& errMsg, const std::string& err, const grpc::Status& status)
    {
        std::string status_err = status.error_message();
        if(status_err.empty())
            status_err = StatusToStr(status.error_code());
        errMsg = err + ", addressUri='" + addressUri + "', err='" + status_err + "'";
    }

    void SetError(std::string& errMsg, const std::string& err)
    {
        errMsg = err + ", addressUri='" + addressUri + "'";
    }

private:
    std::unique_ptr<typename RPC_SERVICE::Stub> stub;
    std::shared_ptr<grpc::ChannelCredentials> creds;

    std::string addressUri;
    std::string error;     // Only used during initialization and later by single-threaded calls

    // Dummy metadata used by no-metadata calls
    static inline const std::map<std::string, std::string> dummy_metadata;
};

template <class RPC_SERVICE>
bool GrpcClient<RPC_SERVICE>::InitFromAddressUri(const std::string& addressUriIn,
                                                 const std::shared_ptr<grpc::ChannelCredentials>& credsIn /*= nullptr*/)
{
    addressUri = addressUriIn;
    creds = (credsIn ? credsIn : grpc::InsecureChannelCredentials());

    // Set the max send and receive message sizes
    grpc::ChannelArguments channelArgs;
    channelArgs.SetMaxSendMessageSize(INT_MAX);
    channelArgs.SetMaxReceiveMessageSize(INT_MAX);
    auto channel = grpc::CreateCustomChannel(addressUri, creds, channelArgs);

    stub = RPC_SERVICE::NewStub(channel);
    if(!stub)
    {
        SetError(error, "Failed to create gRpc service stub");
        return false;
    }
    return true;
}

template <class RPC_SERVICE>
void GrpcClient<RPC_SERVICE>::Reset()
{
    stub.reset();
    creds.reset();
    addressUri.clear();
    error.clear();
}

// Thread-save UNARY gRpc
template <class RPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
bool GrpcClient<RPC_SERVICE>::CallMT(GRPC_STUB_FUNC grpcStubFunc,
                                     const REQ& req, RESP& resp,
                                     const std::map<std::string, std::string>& metadata,
                                     std::string& errMsg)
{
    if(!stub)
    {
        SetError(errMsg, "Invalid (null) gRpc service stub");
        return false;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const std::pair<std::string, std::string>& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Call service
    grpc::Status s = (stub.get()->*grpcStubFunc)(&context, req, &resp);
    if(!s.ok())
    {
        SetError(errMsg, "Failed to make unary call", s);
        return false;
    }

    return true;
}

// Thread-save server-side STREAM gRpc
template <class RPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
bool GrpcClient<RPC_SERVICE>::CallStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                                           const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                                           const std::map<std::string, std::string>& metadata,
                                           std::string& errMsg)
{
    if(!stub)
    {
        SetError(errMsg, "Invalid (null) gRpc service stub");
        return false;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const std::pair<std::string, std::string>& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Call service
    RESP resp;
    std::unique_ptr<grpc::ClientReader<RESP>> reader((stub.get()->*grpcStubFunc)(&context, req));
    while(reader->Read(&resp))
    {
        if(!respCallback(resp))
            context.TryCancel();
        resp.Clear();
    }

    grpc::Status s = reader->Finish();
    if(!s.ok())
    {
        SetError(errMsg, "Failed to make server-side stream call", s);
        return false;
    }

    return true;
}

// Thread-save client-side STREAM gRpc
template <class RPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
bool GrpcClient<RPC_SERVICE>::CallClientStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                                                 ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                                                 const std::map<std::string, std::string>& metadata,
                                                 std::string& errMsg)
{
    if(!stub)
    {
        SetError(errMsg, "Invalid (null) gRpc service stub");
        return false;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const std::pair<std::string, std::string>& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Call service
    REQ req;
    std::unique_ptr<grpc::ClientWriter<REQ>> writer((stub.get()->*grpcStubFunc)(&context, &resp));
    while(reqCallback(req))
    {
        if(!writer->Write(req))
        {
            // Broken stream
            SetError(errMsg, "Failed to make client-side stream call");
            return false;
        }
        req.Clear();
    }
    writer->WritesDone();

    grpc::Status s = writer->Finish();
    if(!s.ok())
    {
        SetError(errMsg, "Failed to make client-side stream call", s);
        return false;
    }

    return true;
}

template <class RPC_SERVICE>
pid_t grpcFork(GrpcClient<RPC_SERVICE>& grpcClient)
{
    // gRpc fork support: Reset GrpcClient before fork()
    std::shared_ptr<grpc::ChannelCredentials> creds;
    std::string addressUri;
    if(grpcClient.IsValid())
    {
        creds = grpcClient.GetCredentials();
        addressUri = grpcClient.GetAddressUri();
        grpcClient.Reset();
    }

    pid_t pid = fork();

    // gRpc fork support: Re-Initialize GrpcClient after fork()
    if(!addressUri.empty())
        grpcClient.InitFromAddressUri(addressUri, creds);

    return pid;
}

} //namespace gen

#endif // __GRPC_CLIENT_HPP__
// *INDENT-ON*


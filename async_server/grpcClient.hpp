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

    GrpcClient(const std::string& host, unsigned short port,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        Init(host, port, creds);
    }

    GrpcClient(const char* domainSocketPath,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        Init(domainSocketPath, creds);
    }

    // To call the server, we need to instantiate a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint specified by addressUri.
    // Note: The channel isn't authenticated by default (use of InsecureChannelCredentials()).

    bool Init(const std::string& host, unsigned short port,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        return InitFromAddressUri(FormatDnsAddressUri(host.c_str(), port), creds);
    }

    bool Init(const char* domainSocketPath,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        return InitFromAddressUri(FormatUnixDomainSocketAddressUri(domainSocketPath), creds);
    }

    bool InitFromAddressUri(const std::string& addressUriIn,
                            const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr);

    // Terminate a channel (if created) and reset GrpcClient to initial state
    void Reset();

    // Thread-save UNARY gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool Call(GRPC_STUB_FUNC grpcStubFunc,
              const REQ& req, RESP& resp,
              const std::map<std::string, std::string>& metadata,
              std::string& errMsg, unsigned long timeout = 0);

    // Thread-save UNARY gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool Call(GRPC_STUB_FUNC grpcStubFunc,
              const REQ& req, RESP& resp,
              std::string& errMsg, unsigned long timeout = 0)
    {
        return Call(grpcStubFunc, req, resp, dummy_metadata, errMsg, timeout);
    }

    // Thread-save server-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStream(GRPC_STUB_FUNC grpcStubFunc,
                    const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                    const std::map<std::string, std::string>& metadata,
                    std::string& errMsg, unsigned long timeout = 0);

    // Thread-save server-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStream(GRPC_STUB_FUNC grpcStubFunc,
                    const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                    std::string& errMsg, unsigned long timeout = 0)
    {
        return CallStream(grpcStubFunc, req, respCallback, dummy_metadata, errMsg, timeout);
    }

    // Thread-save client-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                          ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                          const std::map<std::string, std::string>& metadata,
                          std::string& errMsg, unsigned long timeout = 0);

    // Thread-save client-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                          ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                          std::string& errMsg, unsigned long timeout = 0)
    {
        return CallClientStream(grpcStubFunc, reqCallback, resp, dummy_metadata, errMsg, timeout);
    }

    const std::shared_ptr<grpc::ChannelCredentials> GetCredentials() const { return creds; }
    const std::string GetAddressUri() const { return addressUri; }
    bool IsValid() const { return (bool)stub; }

private:
    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    void SetError(std::string& errOut, const std::string& errIn, const grpc::Status& status = grpc::Status())
    {
        errOut = errIn + ", addressUri='" + addressUri + "'";

        if(!status.ok())
        {
            std::string status_err = status.error_message();
            if(status_err.empty())
                status_err = StatusToStr(status.error_code());
            errOut += ", err='" + status_err + "'";
        }
    }

private:
    std::unique_ptr<typename RPC_SERVICE::Stub> stub;
    std::shared_ptr<grpc::ChannelCredentials> creds;
    std::string addressUri;

    // Dummy metadata used by no-metadata calls
    static inline const std::map<std::string, std::string> dummy_metadata;
};

template <class RPC_SERVICE>
bool GrpcClient<RPC_SERVICE>::InitFromAddressUri(const std::string& addressUriIn,
                                                 const std::shared_ptr<grpc::ChannelCredentials>& credsIn /*=nullptr*/)
{
    addressUri = addressUriIn;
    creds = (credsIn ? credsIn : grpc::InsecureChannelCredentials());

    // Set the max send and receive message sizes
    grpc::ChannelArguments channelArgs;
    channelArgs.SetMaxSendMessageSize(INT_MAX);
    channelArgs.SetMaxReceiveMessageSize(INT_MAX);
    auto channel = grpc::CreateCustomChannel(addressUri, creds, channelArgs);

    stub = RPC_SERVICE::NewStub(channel);
    return (stub != nullptr);
}

template <class RPC_SERVICE>
void GrpcClient<RPC_SERVICE>::Reset()
{
    stub.reset();
    creds.reset();
    addressUri.clear();
}

// Thread-save UNARY gRpc
template <class RPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
bool GrpcClient<RPC_SERVICE>::Call(GRPC_STUB_FUNC grpcStubFunc,
                                   const REQ& req, RESP& resp,
                                   const std::map<std::string, std::string>& metadata,
                                   std::string& errMsg, unsigned long timeout)
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

    // Set deadline of how long to wait for a server reply
    if(timeout > 0)
    {
        std::chrono::time_point<std::chrono::system_clock> deadline =
                std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        context.set_deadline(deadline);
    }

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
bool GrpcClient<RPC_SERVICE>::CallStream(GRPC_STUB_FUNC grpcStubFunc,
                                         const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                                         const std::map<std::string, std::string>& metadata,
                                         std::string& errMsg, unsigned long timeout)
{
    if(!stub)
    {
        SetError(errMsg, "Invalid (null) gRpc service stub");
        return false;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const std::pair<std::string, std::string>& p : metadata)

    // Set deadline of how long to wait for a server reply
    if(timeout > 0)
    {
        std::chrono::time_point<std::chrono::system_clock> deadline =
                std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        context.set_deadline(deadline);
    }

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
bool GrpcClient<RPC_SERVICE>::CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                                               ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                                               const std::map<std::string, std::string>& metadata,
                                               std::string& errMsg, unsigned long timeout)
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

    // Set deadline of how long to wait for a server reply
    if(timeout > 0)
    {
        std::chrono::time_point<std::chrono::system_clock> deadline =
                std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        context.set_deadline(deadline);
    }

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

// Experimantal...
//template <class RPC_SERVICE>
//pid_t grpcFork(GrpcClient<RPC_SERVICE>& grpcClient)
//{
//    // gRpc fork support: Reset GrpcClient before fork()
//    std::shared_ptr<grpc::ChannelCredentials> creds;
//    std::string addressUri;
//    if(grpcClient.IsValid())
//    {
//        creds = grpcClient.GetCredentials();
//        addressUri = grpcClient.GetAddressUri();
//        grpcClient.Reset();
//    }
//
//    pid_t pid = fork();
//
//    // gRpc fork support: Re-Initialize GrpcClient after fork()
//    if(!addressUri.empty())
//        grpcClient.InitFromAddressUri(addressUri, creds);
//
//    return pid;
//}

} //namespace gen

#endif // __GRPC_CLIENT_HPP__
// *INDENT-ON*


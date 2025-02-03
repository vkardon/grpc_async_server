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
#include <functional>

namespace gen {

// Wrapper class to add overload bool() operator that grpc::Status doesn't have.
// This allows to simplify the code when grpc::Status is NOT required:
// if(!grpcClient.Call(...))
//      ....
// However, if grpc::Status is required, then it can be obtained:
// grpc::Status s = grpcClient.Call(...);
// if(!s.ok())
//      ....
struct StatusEx : public grpc::Status
{
    StatusEx(const grpc::Status& s) : grpc::Status(s) {}
    operator bool() { return grpc::Status::ok(); }
};

//
// Helper class to call UNARY/STREAM gRpc service
//
template <class GRPC_SERVICE>
class GrpcClient
{
public:
    GrpcClient() = default;
    ~GrpcClient() = default;

    GrpcClient(const std::string& host, unsigned short port,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
               const grpc::ChannelArguments* channelArgs = nullptr)
    {
        Init(host, port, creds, channelArgs);
    }

    GrpcClient(const std::string& addressUri,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
               const grpc::ChannelArguments* channelArgs = nullptr)
    {
        Init(addressUri, creds, channelArgs);
    }

    // To call the server, we need to instantiate a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint specified by addressUri.
    // Note: The channel isn't authenticated by default (use of InsecureChannelCredentials()).

    bool Init(const std::string& host, unsigned short port,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
              const grpc::ChannelArguments* channelArgs = nullptr)
    {
        return Init(FormatDnsAddressUri(host, port), creds, channelArgs);
    }

    bool Init(const std::string& addressUriIn,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
              const grpc::ChannelArguments* channelArgs = nullptr);

    // Terminate a channel (if created) and reset GrpcClient to the initial state
    void Reset();

    // Thread-save UNARY gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx Call(GRPC_STUB_FUNC grpcStubFunc,
                  const REQ& req, RESP& resp,
                  const std::map<std::string, std::string>& metadata,
                  std::string& errMsg, unsigned long timeout = 0) const;

    // Thread-save UNARY gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx Call(GRPC_STUB_FUNC grpcStubFunc,
                  const REQ& req, RESP& resp,
                  std::string& errMsg, unsigned long timeout = 0) const
    {
        return Call(grpcStubFunc, req, resp, dummy_metadata, errMsg, timeout);
    }

    // Thread-save server-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallStream(GRPC_STUB_FUNC grpcStubFunc,
                        const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                        const std::map<std::string, std::string>& metadata,
                        std::string& errMsg, unsigned long timeout = 0) const;

    // Thread-save server-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallStream(GRPC_STUB_FUNC grpcStubFunc,
                        const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                        std::string& errMsg, unsigned long timeout = 0) const
    {
        return CallStream(grpcStubFunc, req, respCallback, dummy_metadata, errMsg, timeout);
    }

    // Thread-save client-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                              const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                              const std::map<std::string, std::string>& metadata,
                              std::string& errMsg, unsigned long timeout = 0) const;

    // Thread-save client-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                              const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                              std::string& errMsg, unsigned long timeout = 0) const
    {
        return CallClientStream(grpcStubFunc, reqCallback, resp, dummy_metadata, errMsg, timeout);
    }

    const std::shared_ptr<grpc::ChannelCredentials> GetCredentials() const { return creds; }
    const std::shared_ptr<grpc::ChannelArguments> GetChannelArgs() const { return channelArgs; }
    const std::string GetAddressUri() const { return addressUri; }
    bool IsValid() const { return (bool)stub; }

private:
    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    void SetError(std::string& errOut, const std::string& fname,
                  const google::protobuf::Message& req, const grpc::Status& status) const
    {
        errOut = fname + "(" + req.GetTypeName() + ") to uri='" + addressUri + "' error: '";
        if(!status.error_message().empty())
            errOut += status.error_message();
        else
            errOut += StatusToStr(status.error_code());
        errOut += "'";
    }

    // Set deadline of how long to wait for a server reply
    void SetDeadline(grpc::ClientContext& context,  unsigned long timeout) const
    {
        if(timeout > 0)
        {
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
            context.set_deadline(deadline);
        }
    }

private:
    std::unique_ptr<typename GRPC_SERVICE::Stub> stub;
    std::shared_ptr<grpc::ChannelCredentials> creds;
    std::shared_ptr<grpc::ChannelArguments> channelArgs;
    std::string addressUri;

    // Dummy metadata used by no-metadata calls
    static inline const std::map<std::string, std::string> dummy_metadata;
};

template <class GRPC_SERVICE>
bool GrpcClient<GRPC_SERVICE>::Init(const std::string& addressUriIn,
                                    const std::shared_ptr<grpc::ChannelCredentials>& credsIn /*= nullptr*/,
                                    const grpc::ChannelArguments* channelArgsIn /*= nullptr*/)
{
    addressUri = addressUriIn;
    creds = (credsIn ? credsIn : grpc::InsecureChannelCredentials());

    if(channelArgsIn)
    {
        channelArgs = std::make_shared<grpc::ChannelArguments>(*channelArgsIn);
    }
    else
    {
        channelArgs = std::make_shared<grpc::ChannelArguments>();

        // Maximise sent/receive mesage size (instead of 4MB default)
        channelArgs->SetMaxSendMessageSize(INT_MAX);
        channelArgs->SetMaxReceiveMessageSize(INT_MAX);
    }

    std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(addressUri, creds, *channelArgs);
    stub = GRPC_SERVICE::NewStub(channel);
    return (stub != nullptr);
}

template <class GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::Reset()
{
    stub.reset();
    creds.reset();
    channelArgs.reset();
    addressUri.clear();
}

// Thread-save UNARY gRpc
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::Call(GRPC_STUB_FUNC grpcStubFunc,
                                        const REQ& req, RESP& resp,
                                        const std::map<std::string, std::string>& metadata,
                                        std::string& errMsg, unsigned long timeout) const
{
    if(!stub)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) gRpc service stub");
        SetError(errMsg, __func__, req, s);
        return s;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const auto& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Set deadline of how long to wait for a server reply
    SetDeadline(context, timeout);

    // Call service
    grpc::Status s = (stub.get()->*grpcStubFunc)(&context, req, &resp);
    if(!s.ok())
        SetError(errMsg, __func__, req, s);

    return s;
}

// Thread-save server-side STREAM gRpc
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::CallStream(GRPC_STUB_FUNC grpcStubFunc,
                                              const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                                              const std::map<std::string, std::string>& metadata,
                                              std::string& errMsg, unsigned long timeout) const
{
    if(!stub)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) gRpc service stub");
        SetError(errMsg, __func__, req, s);
        return s;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const auto& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Set deadline of how long to wait for a server reply
    SetDeadline(context, timeout);

    // Call service
    RESP resp;
    std::unique_ptr<grpc::ClientReader<RESP>> reader((stub.get()->*grpcStubFunc)(&context, req));
    if(!reader)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) client stream reader");
        SetError(errMsg, __func__, req, s);
        return s;
    }

    while(reader->Read(&resp))
    {
        if(!respCallback(resp))
            context.TryCancel();
        resp.Clear();
    }

    grpc::Status s = reader->Finish();
    if(!s.ok())
        SetError(errMsg, __func__, req, s);

    return s;
}

// Thread-save client-side STREAM gRpc
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                                                    const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                                                    const std::map<std::string, std::string>& metadata,
                                                    std::string& errMsg, unsigned long timeout) const
{
    if(!stub)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) gRpc service stub");
        SetError(errMsg, __func__, REQ(), s);
        return s;
    }

    // Create context and set metadata (if we have any...)
    grpc::ClientContext context;
    for(const auto& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Set deadline of how long to wait for a server reply
    SetDeadline(context, timeout);

    // Call service
    std::unique_ptr<grpc::ClientWriter<REQ>> writer((stub.get()->*grpcStubFunc)(&context, &resp));

    REQ req;
    while(reqCallback(req))
    {
        if(!writer->Write(req))
            break;
        req.Clear();
    }

    writer->WritesDone();

    grpc::Status s = writer->Finish();
    if(!s.ok())
        SetError(errMsg, __func__, req, s);

    return s;
}

// Experimantal...
//template <class GRPC_SERVICE>
//pid_t grpcFork(GrpcClient<GRPC_SERVICE>& grpcClient)
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
//        grpcClient.Init(addressUri, creds);
//
//    return pid;
//}

} //namespace gen

#endif // __GRPC_CLIENT_HPP__
// *INDENT-ON*


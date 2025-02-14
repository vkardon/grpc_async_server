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
#include <mutex>

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

    // UNARY gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx Call(GRPC_STUB_FUNC grpcStubFunc,
                  const REQ& req, RESP& resp,
                  const std::map<std::string, std::string>& metadata,
                  std::string& errMsg, unsigned long timeout = 0);

    // UNARY gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx Call(GRPC_STUB_FUNC grpcStubFunc,
                  const REQ& req, RESP& resp,
                  std::string& errMsg, unsigned long timeout = 0)
    {
        return Call(grpcStubFunc, req, resp, dummy_metadata, errMsg, timeout);
    }

    // Server-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallStream(GRPC_STUB_FUNC grpcStubFunc,
                        const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                        const std::map<std::string, std::string>& metadata,
                        std::string& errMsg, unsigned long timeout = 0);

    // Server-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallStream(GRPC_STUB_FUNC grpcStubFunc,
                        const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                        std::string& errMsg, unsigned long timeout = 0)
    {
        return CallStream(grpcStubFunc, req, respCallback, dummy_metadata, errMsg, timeout);
    }

    // Client-side STREAM gRpc
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                              const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                              const std::map<std::string, std::string>& metadata,
                              std::string& errMsg, unsigned long timeout = 0);

    // Client-side STREAM gRpc - no metadata
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                              const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                              std::string& errMsg, unsigned long timeout = 0)
    {
        return CallClientStream(grpcStubFunc, reqCallback, resp, dummy_metadata, errMsg, timeout);
    }

    void CreateContext(grpc::ClientContext& context,
                       const std::map<std::string, std::string>& metadata,
                       unsigned long timeout) const;

    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    StatusEx GetStream(GRPC_STUB_FUNC grpcStubFunc, const REQ& req,
                       std::unique_ptr<grpc::ClientReader<RESP>>& reader,
                       grpc::ClientContext& context,
                       std::string& errMsg);

    const std::shared_ptr<grpc::ChannelCredentials> GetCredentials() const { return creds; }
    const std::shared_ptr<grpc::ChannelArguments> GetChannelArgs() const { return channelArgs; }
    const std::string GetAddressUri() const { return addressUri; }
    bool IsValid();

    // Terminate a channel (if it exists) and reset GrpcClient to the initial state
    // Note: This method is NOT thread-safe and should not be used when the GrpcClient
    // is shared among multiple threads.
    void Clear();

    // Terminate the channel (if it exists) and initialize the GrpcClient using
    // the same arguments as the last Init() call
    bool Reset();

    void FormatStatusMsg(std::string& errOut, const std::string& fname,
                  const google::protobuf::Message& req,
                  const grpc::Status& status) const;

    void FormatStatusMsg(std::string& errOut, const std::string& fname,
                  const google::protobuf::Message& req,
                  ::grpc::StatusCode statusCode, const std::string& err) const
    {
        return FormatStatusMsg(errOut, fname, req, grpc::Status(statusCode, err));
    }

private:
    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

private:
    std::shared_ptr<typename GRPC_SERVICE::Stub> stub;  // Note: std::shared_ptr to support multithreading
    std::shared_ptr<grpc::ChannelCredentials> creds;
    std::shared_ptr<grpc::ChannelArguments> channelArgs;
    std::string addressUri;
    std::mutex mStubMtx;

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
bool GrpcClient<GRPC_SERVICE>::IsValid()
{
    std::unique_lock<std::mutex> lock(mStubMtx);
    return (bool)stub;
}

// Terminate a channel (if it exists) and reset GrpcClient to the initial state
// Note: This method is NOT thread-safe and should not be used when the GrpcClient
// is shared among multiple threads.
template <class GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::Clear()
{
    stub.reset();
    creds.reset();
    channelArgs.reset();
    addressUri.clear();
}

// Terminate the channel (if it exists) and initialize the GrpcClient using
// the same arguments as the last Init() call
template <class GRPC_SERVICE>
bool GrpcClient<GRPC_SERVICE>::Reset()
{
    if(addressUri.empty())
        return false;

    std::unique_lock<std::mutex> lock(mStubMtx);

    std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(addressUri, creds, *channelArgs);
    stub = GRPC_SERVICE::NewStub(channel);
    return (stub != nullptr);
}

// UNARY gRpc
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::Call(GRPC_STUB_FUNC grpcStubFunc,
                                        const REQ& req, RESP& resp,
                                        const std::map<std::string, std::string>& metadata,
                                        std::string& errMsg, unsigned long timeout)
{
    // Make a local copy of the stub std::shared_ptr.
    // This is to make sure we have a valid stub even if another thread reset stub.
    std::shared_ptr<typename GRPC_SERVICE::Stub> thisStub;

    {
        std::unique_lock<std::mutex> lock(mStubMtx);
        thisStub = stub;
    }

    if(!thisStub)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) gRpc service stub");
        FormatStatusMsg(errMsg, __func__, req, s);
        return s;
    }

    // Create client context
    grpc::ClientContext context;
    CreateContext(context, metadata, timeout);

    // Call service
    grpc::Status s = (thisStub.get()->*grpcStubFunc)(&context, req, &resp);
    if(!s.ok())
        FormatStatusMsg(errMsg, __func__, req, s);

    return s;
}

// Server-side STREAM gRpc
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::CallStream(GRPC_STUB_FUNC grpcStubFunc,
                                              const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                                              const std::map<std::string, std::string>& metadata,
                                              std::string& errMsg, unsigned long timeout)
{
    // Create client context
    grpc::ClientContext context;
    CreateContext(context, metadata, timeout);

    std::unique_ptr<grpc::ClientReader<RESP>> reader;
    StatusEx s = GetStream(grpcStubFunc, req, reader, context, errMsg);
    if(!s.ok())
        return s;

    RESP resp;
    while(reader->Read(&resp))
    {
        if(!respCallback(resp))
            context.TryCancel();
        resp.Clear();
    }

    s = reader->Finish();
    if(!s.ok())
        FormatStatusMsg(errMsg, __func__, req, s);

    return s;
}

// Client-side STREAM gRpc
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                                                    const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                                                    const std::map<std::string, std::string>& metadata,
                                                    std::string& errMsg, unsigned long timeout)
{
    // Make a local copy of the stub std::shared_ptr.
    // This is to make sure we have a valid stub even if another thread reset stub.
    std::shared_ptr<typename GRPC_SERVICE::Stub> thisStub;

    {
        std::unique_lock<std::mutex> lock(mStubMtx);
        thisStub = stub;
    }

    if(!thisStub)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) gRpc service stub");
        FormatStatusMsg(errMsg, __func__, REQ(), s);
        return s;
    }

    // Create client context
    grpc::ClientContext context;
    CreateContext(context, metadata, timeout);

    // Call service
    std::unique_ptr<grpc::ClientWriter<REQ>> writer((thisStub.get()->*grpcStubFunc)(&context, &resp));

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
        FormatStatusMsg(errMsg, __func__, req, s);

    return s;
}

template <class GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::CreateContext(grpc::ClientContext& context,
                                             const std::map<std::string, std::string>& metadata,
                                             unsigned long timeout) const
{
    // Create context and set metadata (if we have any...)
    for(const auto& p : metadata)
        context.AddMetadata(p.first, p.second);

    // Set deadline of how long to wait for a server reply
    if(timeout > 0)
    {
        std::chrono::time_point<std::chrono::system_clock> deadline =
                std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        context.set_deadline(deadline);
    }
}

// Server-side STREAM gRpc - get a stream reader
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
StatusEx GrpcClient<GRPC_SERVICE>::GetStream(GRPC_STUB_FUNC grpcStubFunc, const REQ& req,
                                             std::unique_ptr<grpc::ClientReader<RESP>>& reader,
                                             grpc::ClientContext& context,
                                             std::string& errMsg)
{
    // Make a local copy of the stub std::shared_ptr.
    // This is to make sure we have a valid stub even if another thread reset stub.
    std::shared_ptr<typename GRPC_SERVICE::Stub> thisStub;

    {
        std::unique_lock<std::mutex> lock(mStubMtx);
        thisStub = stub;
    }

    if(!thisStub)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) gRpc service stub");
        FormatStatusMsg(errMsg, __func__, req, s);
        return s;
    }

    // Call service
    RESP resp;
    reader = (thisStub.get()->*grpcStubFunc)(&context, req);
    if(!reader)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) client stream reader");
        FormatStatusMsg(errMsg, __func__, req, s);
        return s;
    }

    return grpc::Status::OK;
}

template <class GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::FormatStatusMsg(std::string& msg, const std::string& fname,
                                               const google::protobuf::Message& req,
                                               const grpc::Status& status) const
{
    msg = fname + "(" + req.GetTypeName() + ") to uri='" + addressUri + "', status: " +
            std::to_string(status.error_code()) + " (" + StatusToStr(status.error_code()) + ")";
    if(!status.error_message().empty())
        msg += ", err: '" + status.error_message() + "'";
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
//        grpcClient.Clear();
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


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
template <typename GRPC_SERVICE>
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

    bool Init(const std::string& addressUri,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
              const grpc::ChannelArguments* channelArgs = nullptr);

    // UNARY gRpc
    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx Call(GRPC_STUB_FUNC grpcStubFunc,
                  const REQ& req, RESP& resp,
                  const std::map<std::string, std::string>& metadata,
                  std::string& errMsg, unsigned long timeout = 0);

    // UNARY gRpc - no metadata
    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx Call(GRPC_STUB_FUNC grpcStubFunc,
                  const REQ& req, RESP& resp,
                  std::string& errMsg, unsigned long timeout = 0)
    {
        return Call(grpcStubFunc, req, resp, dummy_metadata, errMsg, timeout);
    }

    // Server-side STREAM gRpc
    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx CallStream(GRPC_STUB_FUNC grpcStubFunc,
                        const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                        const std::map<std::string, std::string>& metadata,
                        std::string& errMsg, unsigned long timeout = 0);

    // Server-side STREAM gRpc - no metadata
    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx CallStream(GRPC_STUB_FUNC grpcStubFunc,
                        const REQ& req, const std::function<bool(const RESP&)>& respCallback,
                        std::string& errMsg, unsigned long timeout = 0)
    {
        return CallStream(grpcStubFunc, req, respCallback, dummy_metadata, errMsg, timeout);
    }

    // Client-side STREAM gRpc
    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                              const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                              const std::map<std::string, std::string>& metadata,
                              std::string& errMsg, unsigned long timeout = 0);

    // Client-side STREAM gRpc - no metadata
    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                              const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                              std::string& errMsg, unsigned long timeout = 0)
    {
        return CallClientStream(grpcStubFunc, reqCallback, resp, dummy_metadata, errMsg, timeout);
    }

    void CreateContext(grpc::ClientContext& context,
                       const std::map<std::string, std::string>& metadata,
                       unsigned long timeout) const;

    template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
    StatusEx GetStream(GRPC_STUB_FUNC grpcStubFunc, const REQ& req,
                       std::unique_ptr<grpc::ClientReader<RESP>>& reader,
                       grpc::ClientContext& context,
                       std::string& errMsg);

    const std::shared_ptr<grpc::ChannelCredentials> GetCredentials() const { return mCreds; }
    const std::shared_ptr<grpc::ChannelArguments> GetChannelArgs() const { return mChannelArgs; }
    const std::string GetAddressUri() const { return mAddressUri; }
    bool IsValid() const { return (bool)mStub; }

    // Terminate a channel (if it exists) and reset GrpcClient to the initial state
    // Note: This method is NOT thread-safe and should not be used when the GrpcClient
    // is shared among multiple threads.
    void Clear();

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
    std::shared_ptr<typename GRPC_SERVICE::Stub> mStub;  // Note: std::shared_ptr to support multithreading
    std::shared_ptr<grpc::ChannelCredentials> mCreds;
    std::shared_ptr<grpc::ChannelArguments> mChannelArgs;
    std::string mAddressUri;

    // Dummy metadata used by no-metadata calls
    static inline const std::map<std::string, std::string> dummy_metadata;
};

template <typename GRPC_SERVICE>
bool GrpcClient<GRPC_SERVICE>::Init(const std::string& addressUri,
                                    const std::shared_ptr<grpc::ChannelCredentials>& creds /*= nullptr*/,
                                    const grpc::ChannelArguments* channelArgs /*= nullptr*/)
{
    Clear();

    mAddressUri = addressUri;
    mCreds = (creds ? creds : grpc::InsecureChannelCredentials());

    if(channelArgs)
    {
        mChannelArgs = std::make_shared<grpc::ChannelArguments>(*channelArgs);
    }
    else
    {
        mChannelArgs = std::make_shared<grpc::ChannelArguments>();

        // Maximise sent/receive mesage size (instead of 4MB default)
        mChannelArgs->SetMaxSendMessageSize(INT_MAX);
        mChannelArgs->SetMaxReceiveMessageSize(INT_MAX);

        // The gRPC client library has internal backoff policy, which determines
        // how long the client should wait between attempts to re-establish
        // a connection after a transient failure (like a server crash/restart).
        //
        // 1. By default, gRPC uses an Exponential Backoff algorithm. We can tune it
        // to shorten the maximum delay so the client retries more aggressively.
        
        // Set the maximum delay between reconnection attempts to 5 seconds (5,000 milliseconds)
        mChannelArgs->SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 5000); 

        // Set the initial delay to 500ms
        mChannelArgs->SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 500);

        // 2. Enable Aggressive Keepalives to help the client detect a broken connection much faster
        // mChannelArgs->SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);        // Send a ping every 10s
        // mChannelArgs->SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);      // Wait 5s for response
        // mChannelArgs->SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0); // Keep pinging even if idle
    }

    std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(mAddressUri, mCreds, *mChannelArgs);
    if(channel)
        mStub = GRPC_SERVICE::NewStub(channel);
    return (mStub != nullptr);
}

// Terminate a channel (if it exists) and reset GrpcClient to the initial state
// Note: This method is NOT thread-safe and should not be used when the GrpcClient
// is shared among multiple threads.
template <typename GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::Clear()
{
    mStub.reset();
    mCreds.reset();
    mChannelArgs.reset();
    mAddressUri.clear();
}

// UNARY gRpc
template <typename GRPC_SERVICE>
template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
StatusEx GrpcClient<GRPC_SERVICE>::Call(GRPC_STUB_FUNC grpcStubFunc,
                                        const REQ& req, RESP& resp,
                                        const std::map<std::string, std::string>& metadata,
                                        std::string& errMsg, unsigned long timeout)
{
    // Create client context
    grpc::ClientContext context;
    CreateContext(context, metadata, timeout);

    // Call service
    grpc::Status s = (mStub.get()->*grpcStubFunc)(&context, req, &resp);
    if(!s.ok())
        FormatStatusMsg(errMsg, __func__, req, s);

    return s;
}

// Server-side STREAM gRpc
template <typename GRPC_SERVICE>
template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
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
template <typename GRPC_SERVICE>
template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
StatusEx GrpcClient<GRPC_SERVICE>::CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                                                    const std::function<bool(REQ&)>& reqCallback, RESP& resp,
                                                    const std::map<std::string, std::string>& metadata,
                                                    std::string& errMsg, unsigned long timeout)
{
    // Create client context
    grpc::ClientContext context;
    CreateContext(context, metadata, timeout);

    // Call service
    std::unique_ptr<grpc::ClientWriter<REQ>> writer((mStub.get()->*grpcStubFunc)(&context, &resp));

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

template <typename GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::CreateContext(grpc::ClientContext& context,
                                             const std::map<std::string, std::string>& metadata,
                                             unsigned long timeout) const
{
    // Create context and set metadata (if we have any...)
    for(const auto& [key, value] : metadata)
        context.AddMetadata(key, value);

    // Set deadline of how long to wait for a server reply
    if(timeout > 0)
    {
        std::chrono::time_point<std::chrono::system_clock> deadline =
                std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        context.set_deadline(deadline);
    }
}

// Server-side STREAM gRpc - get a stream reader
template <typename GRPC_SERVICE>
template <typename GRPC_STUB_FUNC, typename REQ, typename RESP>
StatusEx GrpcClient<GRPC_SERVICE>::GetStream(GRPC_STUB_FUNC grpcStubFunc, const REQ& req,
                                             std::unique_ptr<grpc::ClientReader<RESP>>& reader,
                                             grpc::ClientContext& context,
                                             std::string& errMsg)
{
    // Call service
    RESP resp;
    reader = (mStub.get()->*grpcStubFunc)(&context, req);
    if(!reader)
    {
        grpc::Status s(grpc::StatusCode::INTERNAL, "Invalid (null) client stream reader");
        FormatStatusMsg(errMsg, __func__, req, s);
        return s;
    }

    return grpc::Status::OK;
}

template <typename GRPC_SERVICE>
void GrpcClient<GRPC_SERVICE>::FormatStatusMsg(std::string& msg, const std::string& fname,
                                               const google::protobuf::Message& req,
                                               const grpc::Status& status) const
{
    msg = fname + "(" + std::string(req.GetTypeName()) + ") to uri='" + mAddressUri + "', status: " +
            std::to_string(status.error_code()) + " (" + StatusToStr(status.error_code()) + ")";
    if(!status.error_message().empty())
        msg += ", err: '" + status.error_message() + "'";
}

} //namespace gen

#endif // __GRPC_CLIENT_HPP__
// *INDENT-ON*


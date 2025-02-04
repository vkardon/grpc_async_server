//
// grpcForwarder.hpp
//
#ifndef __GRPC_FORWARDER_HPP__
#define __GRPC_FORWARDER_HPP__

#include "grpcContext.hpp"      // gen::RpcContext & gen::RpcServerStreamContext
#include "grpcClient.hpp"       // gen::GrpcClient
#include "pipe.hpp"             // Pipe
#include <sstream>              // stringstream

namespace gen {

inline unsigned long UNARY_GRPC_TIMEOUT = 5000; // 5 seconds call timeout. TODO: Configurable?
inline unsigned long PIPE_CAPACITY = 5;         // Max number or requests in pipe. TODO: Configurable?

//
// Helper class to forward UNARY/STREAM a destination gRpc service
//
template <class GRPC_SERVICE>
class GrpcForwarder : protected GrpcClient<GRPC_SERVICE>
{
public:
    GrpcForwarder(const std::string& host, unsigned short port,
                  const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
                  const grpc::ChannelArguments* channelArgs = nullptr)
        : mForwardClient(host, port, creds, channelArgs) {}

    GrpcForwarder(const std::string& addressUri,
                  const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
                  const grpc::ChannelArguments* channelArgs = nullptr)
        : mForwardClient(addressUri, creds, channelArgs) {}

    virtual ~GrpcForwarder() = default;

    // Forward unary request
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    void Forward(const gen::RpcContext& ctx,
                 const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc);

    // Forward server-side stream of requests
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    void Forward(const gen::RpcServerStreamContext& ctx,
                 const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc);

    // Forward client-side stream of requests
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    void Forward(const gen::RpcClientStreamContext& ctx,
                 const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc);

    bool IsValid() const { return mForwardClient.IsValid(); }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& err) const { std::cerr << err << std::endl; }
    virtual void OnInfo(const std::string& info) const { std::cout << info << std::endl; }

    // Copy client metadata from a ServerContext
    static void CopyMetadata(const gen::RpcContext& ctx,
                             std::map<std::string, std::string>& metadata);

private:
    GrpcClient<GRPC_SERVICE> mForwardClient;
};

//
// Helper class to read stream of messages
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcStreamReader
{
public:
    GrpcStreamReader(size_t capacity) : mPipe(capacity) {}
    ~GrpcStreamReader() { Stop(); }

    void Call(const gen::RpcServerStreamContext& ctx,
              const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
              GrpcClient<GRPC_SERVICE>& grpcClient)
    {
        mThread = std::thread([&, grpcStubFunc]()
        {
            // Callback to process stream messages
            std::function respCallback = [&](const RESP& resp) -> bool
            {
//                std::cout << "resp=" << resp << std::endl;
                mPipe.Push(resp);
                return true;
            };

            // Copy client metadata from a ServerContext
            std::map<std::string, std::string> metadata;
            GrpcForwarder<GRPC_SERVICE>::CopyMetadata(ctx, metadata);

            if(!grpcClient.CallStream(grpcStubFunc, req, respCallback, metadata, mErrMsg))
            {
                // Reset the channel to avoid gRPC's internal handling of broken connections
                grpcClient.Reset();

//                std::cerr << mErrMsg << std::endl;
                // Empty the pipe and cause Pop() to return (it anyone waiting)
                mPipe.Clear();
                mGrpcStatus = ::grpc::INTERNAL;
            }
            else
            {
                mGrpcStatus = ::grpc::OK;
            }

            mPipe.SetHasMore(false); // Done reading from the stream
        });
    }

    bool Read(RESP& resp)
    {
        // Note: Pop() return false when nothing left to read
        return mPipe.Pop(resp);
    }

    void Stop()
    {
        // TODO: Cancel reading if we are still in progress...

        // If reader thread is still running, then we have to stop reading first
        if(mThread.joinable())
        {
            // Empty the pipe and cause Pop() to return (it anyone waiting)
            mPipe.Clear();
            mThread.join();
//            std::cerr << "The reader has stopped: status=" << gen::StatusToStr(mGrpcStatus)
//                     << ", req=" << REQ().GetTypeName()
//                     << ", err='" << mErrMsg << "'" << std::endl;
        }
    }

    const std::string& GetError() { return mErrMsg; }
    ::grpc::StatusCode GetStatus() { return mGrpcStatus; }
    bool IsValid() { return mErrMsg.empty(); }

private:
    std::thread mThread;
    Pipe<RESP> mPipe;
    ::grpc::StatusCode mGrpcStatus{::grpc::UNKNOWN};
    std::string mErrMsg;
};

//
// Helper method to copy client metadata from a ServerContext
//
template <class GRPC_SERVICE>
void GrpcForwarder<GRPC_SERVICE>::CopyMetadata(const gen::RpcContext& ctx,
                                               std::map<std::string, std::string>& metadata)
{
    // Copy client metadata from a ServerContext
    for(const auto& pair : ctx.GetServerContext()->client_metadata())
    {
//        std::cout << pair.first << "-->" << pair.second << std::endl;
        metadata[std::string(pair.first.data(), pair.first.size())] = std::string(pair.second.data(), pair.second.size());
    }
}

//
// Forward unary request
//
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
void GrpcForwarder<GRPC_SERVICE>::Forward(const gen::RpcContext& ctx,
                                          const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    // Copy client metadata from a ServerContext
    std::map<std::string, std::string> metadata;
    CopyMetadata(ctx,metadata);

    // Call Grpc Service
    std::string errMsg;
    if(!mForwardClient.Call(grpcStubFunc, req, resp, metadata, errMsg, UNARY_GRPC_TIMEOUT))
    {
        ctx.SetStatus(::grpc::INTERNAL, errMsg);

        // Reset the channel to avoid gRPC's internal handling of broken connections
        mForwardClient.Reset();

        // Note: errMsg already has the request name and the addressUri
        std::stringstream ss;
        ss << __func__ << ":" << __LINE__ <<  " From " << ctx.Peer()
           << ", status=" << gen::StatusToStr(::grpc::INTERNAL)
           << ": " << errMsg;
        OnError(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << "From " << ctx.Peer()
           << ", status=" << gen::StatusToStr(::grpc::OK)
           << ", req=" << req.GetTypeName()
           << ", addressUri='" << mForwardClient.GetAddressUri() << "'";
        OnInfo(ss.str());
    }
}

//
// Forward server-side stream of requests
//
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
void GrpcForwarder<GRPC_SERVICE>::Forward(const gen::RpcServerStreamContext& ctx,
                                          const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    // Start or continue streaming
    auto reader = (GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>*)ctx.GetParam();

    // Are we done?
    if(ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ||
            ctx.GetStreamStatus() == gen::StreamStatus::ERROR)
    {
        // Clean up...
        if(reader)
            delete reader;
        reader = nullptr;
        ctx.SetParam(nullptr);
    }
    else
    {
        // Start or continue streaming
        if(!reader)
        {
            // Create GrpcStreamReader.
            reader = new (std::nothrow) GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>(PIPE_CAPACITY);

            if(!reader)
            {
                ctx.SetHasMore(false); // Stop streaming
                ctx.SetStatus(::grpc::INTERNAL, "Out of memory while allocating a GrpcStreamReader");

                std::stringstream ss;
                ss << __func__ << ":" << __LINE__ <<  " From " << ctx.Peer()
                   << ", status=" << gen::StatusToStr(ctx.GetStatus())
                   << ": " << ctx.GetError();
                OnError(ss.str());
                return;
            }

            reader->Call(ctx, req, grpcStubFunc, mForwardClient);
            ctx.SetParam(reader);
        }

        // Get data to send
        if(reader->Read(resp))
        {
            ctx.SetHasMore(true); // Ask for more data to send
        }
        else if(!reader->IsValid())
        {
            ctx.SetHasMore(false); // No more data to send
            ctx.SetStatus(reader->GetStatus(), reader->GetError());

            // Note: Reader's error already has the request name and the addressUri
            std::stringstream ss;
            ss << __func__ << ":" << __LINE__ <<  " From " << ctx.Peer()
               << ", status=" << gen::StatusToStr(reader->GetStatus())
               << ": " << reader->GetError();
            OnError(ss.str());
        }
        else
        {
            ctx.SetHasMore(false); // No more data to send

            std::stringstream ss;
            ss << "From " << ctx.Peer()
               << ", status=" << gen::StatusToStr(reader->GetStatus())
               << ", req=" << req.GetTypeName()
               << ", addressUri='" << mForwardClient.GetAddressUri() << "'";
            OnInfo(ss.str());
        }
    }


//    if(isFirstResponse)
//    {
//        // This is a very first response.
//        ctx.SetHasMore(true);
//
//        // Create GrpcStreamReader.
//        reader = new (std::nothrow) GrpcStreamReader<SERVICE, SERVICE_FUNC, REQ, RESP>;
//        ctx.SetParam(reader);
//        reader->Call(ctx, req, serviceFunc, addressUri);
//
//        // Always send first response. Come back for more report data if there is any
//    }
//    // If this is NOT a first response and reader has errors, then stop streaming.
//    else if(!reader->IsValid())
//    {
//        ctx.SetHasMore(false); // Nothing to read or reading failed
//        ctx.SetStatus(reader->GetStatus(), reader->GetError());
//        std::cerr << "ERROR: status=" << gen::StatusToStr(reader->GetStatus())
//                << ": " << reader->GetError() << std::endl;
//        return;
//    }
//
//    // Start or continue reading.
//    if(reader->Read(resp))
//    {
//        // Send this response and come back for more.
//        // Note: HasMore is already "true" until set to "false"
//        std::cout << "resp=" << resp << std::endl;
//    }
//    else
//    {
//        // Reading done. Stop streaming.
//        ctx.SetHasMore(false);
//        ctx.SetStatus(reader->GetStatus(), reader->GetError());
//
//        if(reader->IsValid())
//        {
//            std::cout << "status=" << gen::StatusToStr(reader->GetStatus())
//                    << ": req=" << req.GetTypeName() << ", addressUri='" << addressUri << "'" << std::endl;
//        }
//        else
//        {
//            // Note: Reader's error already has the request name and the addressUrl
//            std::cerr << "ERROR: status=" << gen::StatusToStr(reader->GetStatus())
//                    << ": " << reader->GetError() << std::endl;
//        }
//    }
}

//
// Forward client-side stream of requests
//
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
void GrpcForwarder<GRPC_SERVICE>::Forward(const gen::RpcClientStreamContext& ctx,
                                         const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    ctx.SetStatus(::grpc::INTERNAL, "Not Implemented Yet");
}

} //namespace gen


#endif // __GRPC_FORWARDER_HPP__


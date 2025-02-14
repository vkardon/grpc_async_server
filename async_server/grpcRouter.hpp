//
// grpcRouter.hpp
//
#ifndef __GRPC_ROUTER_HPP__
#define __GRPC_ROUTER_HPP__

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
class GrpcRouter
{
public:
    GrpcRouter(const std::string& targetHost, unsigned short targetPort,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
               const grpc::ChannelArguments* channelArgs = nullptr)
        : mTargetClient(targetHost, targetPort, creds, channelArgs) {}

    GrpcRouter(const std::string& targetAddressUri,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
               const grpc::ChannelArguments* channelArgs = nullptr)
        : mTargetClient(targetAddressUri, creds, channelArgs) {}

    virtual ~GrpcRouter() = default;

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

    bool IsValid() const { return mTargetClient.IsValid(); }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& err) const { std::cerr << err << std::endl; }
    virtual void OnInfo(const std::string& info) const { std::cout << info << std::endl; }

    // Copy client metadata from a ServerContext
    static void CopyMetadata(const gen::RpcContext& ctx,
                             std::map<std::string, std::string>& metadata);

private:
    GrpcClient<GRPC_SERVICE> mTargetClient;
};

//
// Helper class to read stream of messages
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcStreamReader
{
public:
    GrpcStreamReader() = default;
    virtual ~GrpcStreamReader() = default;

    // For derived class to override
    virtual void Call(const gen::RpcServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
                      GrpcClient<GRPC_SERVICE>& grpcClient) = 0;
    virtual bool Read(RESP& resp) = 0;
    virtual void Stop() = 0;

    const std::string& GetError() { return mErrMsg; }
    ::grpc::StatusCode GetStatus() { return mGrpcStatus; }
    bool IsValid() { return mErrMsg.empty(); }

protected:
    ::grpc::StatusCode mGrpcStatus{::grpc::UNKNOWN};
    std::string mErrMsg;
};

//
// Asynchronous stream reader to be used when a Router
// is an synchronous gRPC server.
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcAsyncStreamReader : public GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>
{
public:
    GrpcAsyncStreamReader(size_t capacity) : mPipe(capacity) {}
    virtual ~GrpcAsyncStreamReader() = default;

    virtual void Call(const gen::RpcServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
                      GrpcClient<GRPC_SERVICE>& grpcClient) override
    {
        mThread = std::thread([&, grpcStubFunc]()
        {
            // Callback to process stream messages
            std::function respCallback = [&](const RESP& resp) -> bool
            {
                if(!mStop)
                    mPipe.Push(resp);
                return !mStop;
            };

            // Copy client metadata from a ServerContext
            std::map<std::string, std::string> metadata;
            GrpcRouter<GRPC_SERVICE>::CopyMetadata(ctx, metadata);

            if(!grpcClient.CallStream(grpcStubFunc, req, respCallback, metadata, this->mErrMsg))
            {
                // std::cerr << mErrMsg << std::endl;
                // Empty the pipe and cause Pop() to return (it anyone waiting)
                mPipe.Clear();
                this->mGrpcStatus = ::grpc::INTERNAL;

                // Reset the channel to avoid gRPC's internal handling of broken connections
                grpcClient.Reset();
            }
            else
            {
                this->mGrpcStatus = ::grpc::OK;
            }

            mPipe.SetHasMore(false); // Done reading from the stream
        });
    }

    virtual bool Read(RESP& resp) override
    {
        // Note: Pop() return false when nothing left to read
        return mPipe.Pop(resp);
    }

    virtual void Stop() override
    {
        // If the reader thread is still running, we must stop it before proceeding
        mStop = true;
        if(mThread.joinable())
        {
            // Empty the pipe and cause Pop() to return (it anyone waiting)
            mPipe.Clear();
            mThread.join();
        }
    }

private:
    std::thread mThread;
    Pipe<RESP> mPipe;
    bool mStop{false};
};

//
// Synchronous stream reader to be used when a Router
// is an asynchronous gRPC server
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcSyncStreamReader : public GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>
{
public:
    GrpcSyncStreamReader() = default;
    virtual ~GrpcSyncStreamReader() = default;

    virtual void Call(const gen::RpcServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
                      GrpcClient<GRPC_SERVICE>& grpcClient)
    {
        mGrpcClient = &grpcClient;

        // Copy client metadata from a ServerContext
        std::map<std::string, std::string> metadata;
        GrpcRouter<GRPC_SERVICE>::CopyMetadata(ctx, metadata);

        // Create client stream reader
        grpcClient.CreateContext(mContext, metadata, 0);
        if(!grpcClient.GetStream(grpcStubFunc, req, mReader, mContext, this->mErrMsg))
        {
            this->mGrpcStatus = ::grpc::INTERNAL; // Failed to create stream reader
        }
    }

    virtual bool Read(RESP& resp)
    {
        if(!mReader)
            return false;

        if(mReader->Read(&resp))
            return true;

        // Read() returned false, done reading
        if(grpc::Status s = mReader->Finish(); !s.ok())
        {
            mGrpcClient->FormatStatusMsg(this->mErrMsg, __func__, REQ(), s);
            this->mGrpcStatus = ::grpc::INTERNAL;

            // Reset the channel to avoid gRPC's internal handling of broken connections
            mGrpcClient->Reset();
        }
        else
        {
            this->mGrpcStatus = ::grpc::OK;
        }

        mReader.reset();
        return false;
    }

    virtual void Stop()
    {
        if(mReader)
        {
            mContext.TryCancel();
            RESP resp;
            while(mReader->Read(&resp))
                ;
            grpc::Status s = mReader->Finish();
            mGrpcClient->FormatStatusMsg(this->mErrMsg, __func__, REQ(), s);
            this->mGrpcStatus = ::grpc::INTERNAL;
        }
    }

private:
    GrpcClient<GRPC_SERVICE>* mGrpcClient{nullptr};
    std::unique_ptr<grpc::ClientReader<RESP>> mReader;
    grpc::ClientContext mContext;
};

//
// Forward unary request
//
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
void GrpcRouter<GRPC_SERVICE>::Forward(const gen::RpcContext& ctx,
                                          const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    // Copy client metadata from a ServerContext
    std::map<std::string, std::string> metadata;
    CopyMetadata(ctx,metadata);

    // Call Grpc Service
    std::string errMsg;
    if(!mTargetClient.Call(grpcStubFunc, req, resp, metadata, errMsg, UNARY_GRPC_TIMEOUT))
    {
        ctx.SetStatus(::grpc::INTERNAL, errMsg);

        // Reset the channel to avoid gRPC's internal handling of broken connections
        mTargetClient.Reset();

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
           << ", addressUri='" << mTargetClient.GetAddressUri() << "'";
        OnInfo(ss.str());
    }
}

//
// Forward server-side stream of requests
//
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
void GrpcRouter<GRPC_SERVICE>::Forward(const gen::RpcServerStreamContext& ctx,
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
        {
            reader->Stop();
            if(!reader->IsValid())
            {
                // Note: Reader's error already has the request name and the addressUri
                std::stringstream ss;
                ss << __func__ << ":" << __LINE__ <<  " From " << ctx.Peer()
                   << ", status=" << gen::StatusToStr(reader->GetStatus())
                   << ": " << reader->GetError();
                OnError(ss.str());
            }
            delete reader;
        }
        reader = nullptr;
        ctx.SetParam(nullptr);
    }
    else
    {
        // Start or continue streaming
        if(!reader)
        {
            // Create GrpcStreamReader.
            //reader = new (std::nothrow) GrpcAsyncStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>(PIPE_CAPACITY);
            reader = new (std::nothrow) GrpcSyncStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>;

            if(!reader)
            {
                ctx.SetHasMore(false); // Stop streaming
                ctx.SetStatus(::grpc::INTERNAL, "Out of memory while allocating a GrpcAsyncStreamReader");

                std::stringstream ss;
                ss << __func__ << ":" << __LINE__ <<  " From " << ctx.Peer()
                   << ", status=" << gen::StatusToStr(ctx.GetStatus())
                   << ": " << ctx.GetError();
                OnError(ss.str());
                return;
            }

            reader->Call(ctx, req, grpcStubFunc, mTargetClient);
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
               << ", addressUri='" << mTargetClient.GetAddressUri() << "'";
            OnInfo(ss.str());
        }
    }
}

//
// Forward client-side stream of requests
//
template <class GRPC_SERVICE>
template <class GRPC_STUB_FUNC, class REQ, class RESP>
void GrpcRouter<GRPC_SERVICE>::Forward(const gen::RpcClientStreamContext& ctx,
                                         const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    ctx.SetStatus(::grpc::INTERNAL, "Not Implemented Yet");
}

//
// Helper method to copy client metadata from a ServerContext
//
template <class GRPC_SERVICE>
void GrpcRouter<GRPC_SERVICE>::CopyMetadata(const gen::RpcContext& ctx,
                                               std::map<std::string, std::string>& metadata)
{
    // Copy client metadata from a ServerContext
    for(const auto& pair : ctx.GetServerContext()->client_metadata())
    {
//        std::cout << pair.first << "-->" << pair.second << std::endl;
        metadata[std::string(pair.first.data(), pair.first.size())] = std::string(pair.second.data(), pair.second.size());
    }
}

} //namespace gen


#endif // __GRPC_ROUTER_HPP__


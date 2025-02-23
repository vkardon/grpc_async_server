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
    void Forward(const gen::Context& ctx,
                 const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc);

    // Forward server-side stream of requests
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    void Forward(const gen::ServerStreamContext& ctx,
                 const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc);

    // Forward client-side stream of requests
    template <class GRPC_STUB_FUNC, class REQ, class RESP>
    void Forward(const gen::ClientStreamContext& ctx,
                 const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc);

    bool IsValid() const { return mTargetClient.IsValid(); }

    // For derived class to override (Error and Info reporting)
    virtual void OnError(const std::string& err) const { std::cerr << err << std::endl; }
    virtual void OnInfo(const std::string& info) const { std::cout << info << std::endl; }

    // Helper method to get client metadata
    static void GetMetadata(const grpc::ServerContext& ctx, std::map<std::string, std::string>& metadata);

private:
    // Helper method to format error
    std::string FormatError(const std::string& fname, int lineNumber,
                            const std::string& from, const google::protobuf::Message& req,
                            const ::grpc::Status& status) const;

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
    virtual void Call(const gen::ServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
                      GrpcClient<GRPC_SERVICE>& grpcClient) = 0;
    virtual bool Read(RESP& resp) = 0;
    virtual void Stop() = 0;

    const ::grpc::Status& GetStatus() { return mStatus; }
    const std::string& GetPeer() { return mPeer; }
    bool IsValid() { return mStatus.ok(); }

protected:
    ::grpc::Status mStatus{::grpc::Status::OK};
    std::string mPeer;
};

//
// Asynchronous stream reader to be used with a synchronous gRPC server.
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcAsyncStreamReader : public GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>
{
public:
    GrpcAsyncStreamReader(size_t capacity) : mPipe(capacity) {}
    virtual ~GrpcAsyncStreamReader() = default;

    virtual void Call(const gen::ServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
                      GrpcClient<GRPC_SERVICE>& grpcClient) override
    {
        this->mPeer = ctx.Peer();
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
            GrpcRouter<GRPC_SERVICE>::GetMetadata(ctx, metadata);

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
// Synchronous stream reader to be used with an asynchronous gRPC server
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcSyncStreamReader : public GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>
{
public:
    GrpcSyncStreamReader() = default;
    virtual ~GrpcSyncStreamReader() = default;

    virtual void Call(const gen::ServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc,
                      GrpcClient<GRPC_SERVICE>& grpcClient) override
    {
        this->mPeer = ctx.Peer();
        mGrpcClient = &grpcClient;

        // Copy client metadata from a ServerContext
        std::map<std::string, std::string> metadata;
        GrpcRouter<GRPC_SERVICE>::GetMetadata(ctx, metadata);

        // Create client stream reader
        std::string errMsg;
        grpcClient.CreateContext(mContext, metadata, 0);
        if(!grpcClient.GetStream(grpcStubFunc, req, mReader, mContext, errMsg))
        {
            this->mStatus = { ::grpc::INTERNAL, errMsg };
        }
    }

    virtual bool Read(RESP& resp) override
    {
        if(!mReader)
            return false;

        if(mReader->Read(&resp))
            return true;

        // Read() returned false, done reading
        if(grpc::Status s = mReader->Finish(); !s.ok())
        {
            std::string errMsg;
            mGrpcClient->FormatStatusMsg(errMsg, __func__, REQ(), s);
            this->mStatus = { ::grpc::INTERNAL, errMsg };

            // Reset the channel to avoid gRPC's internal handling of broken connections
            mGrpcClient->Reset();
        }
        else
        {
            this->mStatus = ::grpc::Status::OK;
        }

        mReader.reset();
        return false;
    }

    virtual void Stop() override
    {
        if(mReader)
        {
            mContext.TryCancel();
            RESP resp;
            while(mReader->Read(&resp))
                ;
            std::string errMsg;
            grpc::Status s = mReader->Finish();
            mGrpcClient->FormatStatusMsg(errMsg, __func__, REQ(), s);
            this->mStatus = { ::grpc::INTERNAL, errMsg };
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
void GrpcRouter<GRPC_SERVICE>::Forward(const gen::Context& ctx,
                                       const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    // Check for Deadline Expiration.
    // Get the Deadline (std::chrono::time_point) and calculate the remaining time.
    std::chrono::time_point<std::chrono::system_clock> deadline = ctx.deadline();
    auto remainingTime = deadline - std::chrono::system_clock::now();

    if(remainingTime <= std::chrono::milliseconds(0))
    {
        ctx.SetStatus(::grpc::DEADLINE_EXCEEDED, "Request already past deadline");
        std::string err = FormatError(__func__, __LINE__, ctx.Peer(), req, ctx.GetStatus());
        OnError(err);
        return;
    }

    // Limit forward timeout not to exceed UNARY_GRPC_TIMEOUT
    unsigned long timeout = std::chrono::duration_cast<std::chrono::milliseconds>(remainingTime).count();
    if(timeout > UNARY_GRPC_TIMEOUT)
        timeout = UNARY_GRPC_TIMEOUT;

    // Copy client metadata from a ServerContext
    std::map<std::string, std::string> metadata;
    GetMetadata(ctx, metadata);

    // Call Grpc Service
    std::string errMsg;
    if(!mTargetClient.Call(grpcStubFunc, req, resp, metadata, errMsg, timeout))
    {
        // Reset the channel to avoid gRPC's internal handling of broken connections
        mTargetClient.Reset();
        ctx.SetStatus(::grpc::INTERNAL, errMsg);
        std::string err = FormatError(__func__, __LINE__, ctx.Peer(), req, ctx.GetStatus());
        OnError(err);
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
void GrpcRouter<GRPC_SERVICE>::Forward(const gen::ServerStreamContext& ctx,
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
                std::string err = FormatError(__func__, __LINE__, reader->GetPeer(), req, reader->GetStatus());
                OnError(err);
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
                // Stop streaming
                ctx.EndOfStream(::grpc::INTERNAL, "Out of memory while allocating a GrpcAsyncStreamReader");
                std::string err = FormatError(__func__, __LINE__, ctx.Peer(), req, ctx.GetStatus());
                OnError(err);
                return;
            }

            reader->Call(ctx, req, grpcStubFunc, mTargetClient);
            ctx.SetParam(reader);
        }

        // Get data to send
        if(reader->Read(resp))
        {
            // We read some data, send it and come back for more
        }
        else if(!reader->IsValid())
        {
            const ::grpc::Status& s = reader->GetStatus();
            ctx.EndOfStream(s.error_code(), s.error_message());
            std::string err = FormatError(__func__, __LINE__, ctx.Peer(), req, ctx.GetStatus());
            OnError(err);
        }
        else
        {
            ctx.EndOfStream(); // No more data to send

            std::stringstream ss;
            ss << "From " << ctx.Peer()
               << ", status=" << gen::StatusToStr(reader->GetStatus().error_code())
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
void GrpcRouter<GRPC_SERVICE>::Forward(const gen::ClientStreamContext& ctx,
                                       const REQ& req, RESP& resp, GRPC_STUB_FUNC grpcStubFunc)
{
    ctx.SetStatus(::grpc::INTERNAL, "Not Implemented Yet");
}

//
// Helper method to format errorcd
//
template <class GRPC_SERVICE>
std::string GrpcRouter<GRPC_SERVICE>::FormatError(const std::string& fname, int lineNumber,
                                                  const std::string& from, const google::protobuf::Message& req,
                                                  const ::grpc::Status& status) const
{
    std::stringstream ss;
    ss << fname << ":" << lineNumber <<  "(" + req.GetTypeName() + ") from " << from
    << ", status: " << status.error_code() << " (" << gen::StatusToStr(status.error_code()) << ")"
    << ", err: '" << status.error_message() << "'";
    return ss.str();
}

//
// Helper method to get client metadata
//
template <class GRPC_SERVICE>
void GrpcRouter<GRPC_SERVICE>::GetMetadata(const grpc::ServerContext& ctx, 
                                           std::map<std::string, std::string>& metadata)
{
    for(const auto& pair : ctx.client_metadata())
    {
        metadata[std::string(pair.first.data(), pair.first.size())] = std::string(pair.second.data(), pair.second.size());
    }
}

} //namespace gen


#endif // __GRPC_ROUTER_HPP__


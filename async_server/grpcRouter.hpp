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
    GrpcRouter(bool asyncForward = false) : mAsyncForward(asyncForward) {}
    virtual ~GrpcRouter() = default;

    GrpcRouter(const std::string& targetHost, unsigned short targetPort,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
               const grpc::ChannelArguments* channelArgs = nullptr)
        : mTargetClient(targetHost, targetPort, creds, channelArgs) {}

    GrpcRouter(const std::string& targetAddressUri,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
               const grpc::ChannelArguments* channelArgs = nullptr)
        : mTargetClient(targetAddressUri, creds, channelArgs) {}

    bool Init(const std::string& host, unsigned short port,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
              const grpc::ChannelArguments* channelArgs = nullptr)
    {
        return mTargetClient.Init(host, port, creds, channelArgs);
    }

    bool Init(const std::string& addressUriIn,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr,
              const grpc::ChannelArguments* channelArgs = nullptr)
    {
        return mTargetClient.Init(addressUriIn, creds, channelArgs);
    }

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
    virtual void OnError(const std::string& fname, int lineNum, const std::string& func,
                         const std::string& err) const;
    virtual void OnInfo(const std::string& fname, int lineNum, const std::string& func,
                        const std::string& info) const;

    // Helper method to get client metadata
    void GetMetadata(const grpc::ServerContext& ctx, std::map<std::string, std::string>& metadata) const;

    // Helper method to format error
    std::string FormatError(const std::string& from, const google::protobuf::Message& req,
                            const ::grpc::Status& status) const;

    GrpcClient<GRPC_SERVICE>& GetTargetClient() { return mTargetClient; }

private:
    GrpcClient<GRPC_SERVICE> mTargetClient;
    bool mAsyncForward{false};
};

//
// Helper class to read stream of messages
//
template <class GRPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
class GrpcStreamReader
{
public:
    GrpcStreamReader(GrpcRouter<GRPC_SERVICE>* router) : mRouter(router) {}
    virtual ~GrpcStreamReader() = default;

    // For derived class to override
    virtual void Call(const gen::ServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc) = 0;
    virtual bool Read(RESP& resp) = 0;
    virtual void Stop() = 0;

    const ::grpc::Status& GetStatus() { return mStatus; }
    const std::string& GetPeer() { return mPeer; }
    bool IsValid() { return mStatus.ok(); }

protected:
    GrpcRouter<GRPC_SERVICE>* mRouter{nullptr};
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
    GrpcAsyncStreamReader(GrpcRouter<GRPC_SERVICE>* router, size_t capacity)
        : GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>(router),
          mPipe(capacity) {}
    virtual ~GrpcAsyncStreamReader() = default;

    virtual void Call(const gen::ServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc) override
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
            this->mRouter->GetMetadata(ctx, metadata);
            std::string errMsg;

            GrpcClient<GRPC_SERVICE>& grpcClient = this->mRouter->GetTargetClient();
            if(!grpcClient.CallStream(grpcStubFunc, req, respCallback, metadata, errMsg))
            {
                // std::cerr << errMsg << std::endl;
                // Empty the pipe and cause Pop() to return (it anyone waiting)
                mPipe.Clear();
                this->mStatus = { ::grpc::INTERNAL, errMsg };
                errMsg = this->mRouter->FormatError(this->mPeer, req, this->mStatus);
                this->mRouter->OnError(__FNAME__, __LINE__, __func__, errMsg);

                // Reset the channel to avoid gRPC's internal handling of broken connections
                grpcClient.Reset();
            }
            else
            {
                this->mStatus = ::grpc::Status::OK;
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
    GrpcSyncStreamReader(GrpcRouter<GRPC_SERVICE>* router)
        : GrpcStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>(router),
          mGrpcClient(router->GetTargetClient()) {}
    virtual ~GrpcSyncStreamReader() = default;

    virtual void Call(const gen::ServerStreamContext& ctx,
                      const REQ& req, GRPC_STUB_FUNC grpcStubFunc) override
    {
        this->mPeer = ctx.Peer();

        // Copy client metadata from a ServerContext
        std::map<std::string, std::string> metadata;
        this->mRouter->GetMetadata(ctx, metadata);

        // Create client stream reader
        std::string errMsg;
        mGrpcClient.CreateContext(mClientContext, metadata, 0);
        if(!mGrpcClient.GetStream(grpcStubFunc, req, mReader, mClientContext, errMsg))
        {
            this->mStatus = { ::grpc::INTERNAL, errMsg };
            errMsg = this->mRouter->FormatError(this->mPeer, req, this->mStatus);
            this->mRouter->OnError(__FNAME__, __LINE__, __func__, errMsg);
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
            mGrpcClient.FormatStatusMsg(errMsg, __func__, REQ(), s);
            this->mStatus = { ::grpc::INTERNAL, errMsg };
            errMsg = this->mRouter->FormatError(this->mPeer, REQ(), this->mStatus);
            this->mRouter->OnError(__FNAME__, __LINE__, __func__, errMsg);

            // Reset the channel to avoid gRPC's internal handling of broken connections
            mGrpcClient.Reset();
        }
        else
        {
            this->mStatus = ::grpc::Status::OK;
        }

        // Reading is done
        mReader.reset();    // Reset to tell Stop() that there is nothing to cancel
        return false;       // Reading is done
    }

    virtual void Stop() override
    {
        if(mReader)
        {
            mClientContext.TryCancel();
            RESP resp;
            while(mReader->Read(&resp))
                ;
            std::string errMsg;
            grpc::Status s = mReader->Finish();
            mGrpcClient.FormatStatusMsg(errMsg, __func__, REQ(), s);
            this->mStatus = { ::grpc::INTERNAL, errMsg };
            errMsg = this->mRouter->FormatError(this->mPeer, REQ(), this->mStatus);
            this->mRouter->OnError(__FNAME__, __LINE__, __func__, errMsg);
        }
    }

private:
    GrpcClient<GRPC_SERVICE>& mGrpcClient;
    grpc::ClientContext mClientContext;
    std::unique_ptr<grpc::ClientReader<RESP>> mReader;
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
        std::string err = FormatError(ctx.Peer(), req, ctx.GetStatus());
        OnError(__FNAME__, __LINE__, __func__, err);
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
        std::string err = FormatError(ctx.Peer(), req, ctx.GetStatus());
        OnError(__FNAME__, __LINE__, __func__, err);
    }
    else
    {
        std::stringstream ss;
        ss << "From " << ctx.Peer()
           << ", status=" << gen::StatusToStr(::grpc::OK)
           << ", req=" << req.GetTypeName()
           << ", addressUri='" << mTargetClient.GetAddressUri() << "'";
        OnInfo(__FNAME__, __LINE__, __func__, ss.str());
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
            reader->Stop(); // Note: Stop() does nothing if the reader is already complete
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
            if(mAsyncForward)
                reader = new (std::nothrow) GrpcAsyncStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>(this, PIPE_CAPACITY);
            else
                reader = new (std::nothrow) GrpcSyncStreamReader<GRPC_SERVICE, GRPC_STUB_FUNC, REQ, RESP>(this);

            if(!reader)
            {
                // Stop streaming
                ctx.EndOfStream(::grpc::INTERNAL, "Out of memory while allocating a GrpcAsyncStreamReader");
                std::string err = FormatError(ctx.Peer(), req, ctx.GetStatus());
                OnError(__FNAME__, __LINE__, __func__, err);
                return;
            }

            ctx.SetParam(reader);
            reader->Call(ctx, req, grpcStubFunc);
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
        }
        else
        {
            ctx.EndOfStream(); // No more data to send

            std::stringstream ss;
            ss << "From " << ctx.Peer()
               << ", status=" << gen::StatusToStr(reader->GetStatus().error_code())
               << ", req=" << req.GetTypeName()
               << ", addressUri='" << mTargetClient.GetAddressUri() << "'";
            OnInfo(__FNAME__, __LINE__, __func__, ss.str());
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
// For derived class to override (Error and Info reporting)
//
template <class GRPC_SERVICE>
void GrpcRouter<GRPC_SERVICE>::OnError(const std::string& fname, int lineNum, const std::string& func,
                                       const std::string& err) const
{
    std::cout << "ERROR: " << fname << ":" << lineNum << " " << func << "(), " << err << std::endl;
}

template <class GRPC_SERVICE>
void GrpcRouter<GRPC_SERVICE>::OnInfo(const std::string& fname, int lineNum, const std::string& func,
                                      const std::string& info) const
{
    std::cout << "INFO: " << fname << ":" << lineNum << " " << func << "(), " << info << std::endl;
}

//
// Helper method to format errorcd
//
template <class GRPC_SERVICE>
std::string GrpcRouter<GRPC_SERVICE>::FormatError(const std::string& from, const google::protobuf::Message& req,
                                                  const ::grpc::Status& status) const
{
    std::stringstream ss;
    ss << "(" + req.GetTypeName() + ") from " << from
    << ", status: " << status.error_code() << " (" << gen::StatusToStr(status.error_code()) << ")"
    << ", err: '" << status.error_message() << "'";
    return ss.str();
}

//
// Helper method to get client metadata
//
template <class GRPC_SERVICE>
void GrpcRouter<GRPC_SERVICE>::GetMetadata(const grpc::ServerContext& ctx, 
                                           std::map<std::string, std::string>& metadata) const
{
    for(const auto& pair : ctx.client_metadata())
    {
        metadata[std::string(pair.first.data(), pair.first.size())] = std::string(pair.second.data(), pair.second.size());
    }
}


} //namespace gen


#endif // __GRPC_ROUTER_HPP__


//
// proxyUtils.hpp
//
#ifndef __PROXY_UTILS_HPP__
#define __PROXY_UTILS_HPP__

#include "grpcContext.hpp"          // gen::RpcContext & gen::RpcServerStreamContext
#include <thread>
#include <condition_variable>
#include <memory>

inline unsigned long GRPC_TIMEOUT = 5000; // 5 seconds call timeout. TODO: Configurable?

template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
void Forward(const gen::RpcContext& ctx,
             const REQ& req, RESP& resp, SERVICE_FUNC serviceFunc,
             const std::string& host, unsigned short port)
{
//    // Validate session id
//    if(!ctx.GetSession())
//    {
//        errMsg = "Invalid session id";
//        ERRORMSG(Log(), ctx << ", req=" << req.GetTypeName() << ", err='" << errMsg << "'");
//        return ::grpc::UNAUTHENTICATED;
//    }

    // Call Grpc Service
    grpc::ClientContext context;
//    context.AddMetadata(GRPC_METADATA_SESSION_ID, ctx.mSessionId);
//    context.AddMetadata(GRPC_METADATA_REQUEST_ID, ctx.mRequestId);
//    context.AddMetadata(GRPC_METADATA_IP_ADDR, ctx.GetPeer());
//    context.AddMetadata(GRPC_METADATA_USER, ctx.mSession->GetUserShmAddress());

    std::string addressUri = gen::FormatDnsAddressUri(host.c_str(), port);

    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(addressUri, grpc::InsecureChannelCredentials());
    std::unique_ptr<typename SERVICE::Stub> stub = SERVICE::NewStub(channel);
    if(!stub.get())
    {
        std::string errMsg = "Failed to create Grpc Service stub, req=" +
                 req.GetTypeName() + ", addressUri='" + addressUri + "'";
        ctx.SetStatus(::grpc::INTERNAL, errMsg);
        return;
    }

    // Set deadline of how long to wait for a server reply
    std::chrono::time_point<std::chrono::system_clock> deadline =
        std::chrono::system_clock::now() + std::chrono::milliseconds(GRPC_TIMEOUT);
    context.set_deadline(deadline);

    grpc::Status s = (stub.get()->*serviceFunc)(&context, req, &resp);
    if(!s.ok())
    {
        std::string errMsg = "Failed to call Grpc Service, req=" +
                 req.GetTypeName() + ", addressUri='" + addressUri + "', err='" + s.error_message() + "'";
        ctx.SetStatus(::grpc::INTERNAL, errMsg);
    }
}


//
// Helper class to read stream of messages
//
template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
class GrpcStreamReader
{
public:
    GrpcStreamReader(const std::string& repmgrHost, unsigned short repmgrPort)
    {
        mAddressUri = gen::FormatDnsAddressUri(repmgrHost.c_str(), repmgrPort);
    }
    ~GrpcStreamReader() { Stop(); }

    bool Call(const gen::RpcServerStreamContext& ctx,
              const REQ& req, SERVICE_FUNC serviceFunc)
    {
//        // Validate session id
//        mContext.reset(new (std::nothrow) Context(sessionMgr, ctx));
//        if(!mContext)
//        {
//            mErrMsg = "Out of memory allocating Context";
//            mGrpcStatus = ::grpc::INTERNAL;
//            return false;
//        }
//
//        if(!mContext->GetSession())
//        {
//            mErrMsg = "Invalid session id";
//            mGrpcStatus = ::grpc::UNAUTHENTICATED;
//            return false;
//        }

        mContextStr = "From " + ctx.Peer();

        // Call Grpc Service
        mClientContext.reset(new (std::nothrow) grpc::ClientContext);
        if(!mClientContext.get())
        {
            mErrMsg = "Out of memory allocating grpc::ClientContext";
            mGrpcStatus = ::grpc::INTERNAL;
            return false;
        }

//        mClientContext->AddMetadata(GRPC_METADATA_SESSION_ID, mContext->mSessionId);
//        mClientContext->AddMetadata(GRPC_METADATA_REQUEST_ID, mContext->mRequestId);
//        mClientContext->AddMetadata(GRPC_METADATA_IP_ADDR, mContext->GetPeer());
//        mClientContext->AddMetadata(GRPC_METADATA_USER, mContext->mSession->GetUserShmAddress());

        mChannel = grpc::CreateChannel(mAddressUri, grpc::InsecureChannelCredentials());
        mStub = SERVICE::NewStub(mChannel);
        if(!mStub.get())
        {
            mErrMsg = "Failed to create Grpc Service stub, req=" +
                      req.GetTypeName() + ", addressUri='" + mAddressUri + "'";
            mGrpcStatus = ::grpc::INTERNAL;
            return false;
        }

        // Note: We can't set deadline on streaming calls since reading report back might
        // take a significant amount of time for a large report files.
//        // Set deadline of how long to wait for a server reply
//        std::chrono::time_point<std::chrono::system_clock> deadline =
//            std::chrono::system_clock::now() + std::chrono::milliseconds(GRPC_TIMEOUT);
//        mClientContext->set_deadline(deadline);

        mReader = (mStub.get()->*serviceFunc)(mClientContext.get(), req);
        if(!mReader.get())
        {
            mErrMsg = "Failed to create Grpc Service Client Reader, req=" +
                      req.GetTypeName() + ", addressUri='" + mAddressUri + "'";
            mGrpcStatus = ::grpc::INTERNAL;
            return false;
        }

        return true;
    }

    bool Read(RESP& resp, bool async)
    {
        return (async ? ReadAsyncImpl(resp) : ReadImpl(resp));
    }

    void Stop()
    {
        // If reader thread is still running, then we have to stop reading first
        if(mReadThread.joinable())
        {
            mDoneReadng = true; // To cause reader thread to cancel reading and exit
            mReadThread.join();
            std::cerr << ContextStr() << ", status=" << gen::StatusToStr(mGrpcStatus)
                     << ": The reader has stopped, req=" << REQ().GetTypeName()
                     << ", addressUri='" << mAddressUri << "', err='" << mErrMsg << "'" << std::endl;
        }
    }

    const std::string& GetError() { return mErrMsg; }
    ::grpc::StatusCode GetStatus() { return mGrpcStatus; }
    const std::string ContextStr() { return mContextStr; }
    const std::string GetAddressUri() { return mAddressUri; }

    bool IsValid() { return mErrMsg.empty(); }

private:
    // Synchronous reading
    // Read one message per call
    bool ReadImpl(RESP& resp)
    {
        if(mReader->Read(&resp))
            return true;

        // No more messages to read (or reading failed). Update call status
        grpc::Status s = mReader->Finish();
        mGrpcStatus = s.error_code();
        if(!s.ok())
            mErrMsg = "Failed to call Grpc Service, req=" +
                      REQ().GetTypeName() + ", addressUri='" + mAddressUri + "', err='" + s.error_message() + "'";

        return false; // Done reading
    }

    // Asynchronous reading
    // Use worker thread to asynchronously read messages as they coming in.
    bool ReadAsyncImpl(RESP& resp)
    {
        // Create thread in a first call
        if(!mReadThread.joinable())
        {
            mReadThread = std::thread([&]()
            {
                ::grpc::StatusCode grpcStatus{::grpc::UNKNOWN};
                std::string errMsg;

                while(true)
                {
                    RESP* pResp = new (std::nothrow) RESP;
                    if(!pResp)
                    {
                        errMsg = "Out of memory allocating response message " + resp.GetTypeName();
                        grpcStatus = ::grpc::INTERNAL;
                        break;
                    }

                    // Note: mDoneReadng might be set by Stop() to cancel reading
                    if(mDoneReadng)
                        mClientContext->TryCancel(); // Will cause Read() to return false;

                    if(!mReader->Read(pResp))
                    {
                        // No more messages to read (or reading failed)
                        delete pResp;
                        grpc::Status s = mReader->Finish();

                        // Update call status
                        grpcStatus = s.error_code();
                        if(!s.ok())
                            errMsg = "Failed to call Grpc Service, req=" +
                                     REQ().GetTypeName() + ", addressUri='" + mAddressUri + "', err='" + s.error_message() + "'";

                        break; // Done reading
                    }

                    // Add messages to the list
                    std::unique_lock<std::mutex> lock(mReadMutex);
                    mRespList.emplace_back(pResp);
                    mReadCv.notify_one();
                }

                std::unique_lock<std::mutex> lock(mReadMutex);
                mErrMsg = errMsg;
                mGrpcStatus = grpcStatus;
                mDoneReadng = true;
                mReadCv.notify_one();

            }); // End of thread lambda
        }

        // Wait for a new message
        std::unique_lock<std::mutex> lock(mReadMutex);
        while(mRespList.empty() && !mDoneReadng)
            mReadCv.wait(lock);

        // Do we have any messages?
        if(mErrMsg.empty() && !mRespList.empty())
        {
            // Pop the front message
            RESP* pResp = mRespList.front().get();
            resp = *pResp;
            mRespList.pop_front();
            return true;
        }

        // We must be here because reading is done
        assert(mDoneReadng);
        mReadThread.join();
        return false;
    }

private:
    // Grps service variables
    std::string mAddressUri;
    std::shared_ptr<grpc::Channel> mChannel;
    std::unique_ptr<typename SERVICE::Stub> mStub;
    std::unique_ptr<grpc::ClientReader<RESP>> mReader;
    std::unique_ptr<grpc::ClientContext> mClientContext;

    ::grpc::StatusCode mGrpcStatus{::grpc::UNKNOWN};
    std::string mErrMsg;
    std::string mContextStr;

    // For asynchronous reading
    std::list<std::unique_ptr<RESP>> mRespList;
    std::thread mReadThread;
    std::mutex mReadMutex;
    std::condition_variable mReadCv;
    bool mDoneReadng{false};
};

template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
void Forward(const gen::RpcServerStreamContext& ctx,
             const REQ& req, RESP& resp, SERVICE_FUNC serviceFunc,
             const std::string& host, unsigned short port)
{
    // Start or continue streaming
    GrpcStreamReader<SERVICE, SERVICE_FUNC, REQ, RESP>* reader =
        (GrpcStreamReader<SERVICE, SERVICE_FUNC, REQ, RESP>*)ctx.GetParam();
    bool isFirstResponse = !reader;

    // Are we done?
    if(ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ||
            ctx.GetStreamStatus() == gen::StreamStatus::ERROR)
    {
        // Clean up...
        if(reader)
            delete reader;
        reader = nullptr;
        ctx.SetParam(nullptr);
        return;
    }

    if(isFirstResponse)
    {
        // This is a very first response. Create GrpcStreamReader.
        reader = new (std::nothrow) GrpcStreamReader<SERVICE, SERVICE_FUNC, REQ, RESP>(host, port);
        ctx.SetParam(reader);

        // Always send first response. Come back for more report data if there is any
        ctx.SetHasMore(true);

        // Call Grpc Service.
        if(!reader->Call(ctx, req, serviceFunc))
        {
            // If failed, then just return. Next call will stop streaming.
            assert(!reader->IsValid());
            return;
        }
    }
    // If this is NOT a first response and reader has errors, then stop streaming.
    else if(!reader->IsValid())
    {
        ctx.SetHasMore(false); // Nothing to read or reading failed
        ctx.SetStatus(reader->GetStatus(), reader->GetError());
        std::cerr << reader->ContextStr() << ", status=" << gen::StatusToStr(reader->GetStatus())
                 << ": " << reader->GetError() << std::endl;
        return;
    }

    // Start or continue reading.
//    if(reader->Read(resp, true /*async*/))
    if(reader->Read(resp, false /*sync*/))
    {
        // Send this response and come back for more.
//        ctx.SetHasMore(true); // Already "true" until set to "false"
    }
    else
    {
        // Reading done. Stop streaming.
        ctx.SetHasMore(false);
        ctx.SetStatus(reader->GetStatus(), reader->GetError());

        if(reader->IsValid())
        {
            std::cout << reader->ContextStr() << ", status=" << gen::StatusToStr(reader->GetStatus())
                      << ": req=" << req.GetTypeName() << ", addressUri='" << reader->GetAddressUri() << "'" << std::endl;
        }
        else
        {
            // Note: Reader's error already has the request name and the addressUrl
            std::cerr << reader->ContextStr() << ", status=" << gen::StatusToStr(reader->GetStatus())
                     << ": " << reader->GetError() << std::endl;
        }

        // Add duration to the server trailing metadata.
//        reader->SetDuration();
    }
}

template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
void Forward(const gen::RpcClientStreamContext& ctx,
             const REQ& req, RESP& resp, SERVICE_FUNC serviceFunc,
             const std::string& host, unsigned short port)
{
    ctx.SetStatus(::grpc::INTERNAL, "Not Implemented Yet");
}

#endif // __PROXY_UTILS_HPP__


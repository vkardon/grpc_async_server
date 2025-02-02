//
// proxyUtils.hpp
//
#ifndef __PROXY_UTILS_HPP__
#define __PROXY_UTILS_HPP__

#include "grpcContext.hpp"      // gen::RpcContext & gen::RpcServerStreamContext
#include "grpcClient.hpp"       // gen::GrpcClient
//#include <thread>
//#include <condition_variable>
//#include <memory>
#include "pipe.hpp"

inline unsigned long GRPC_TIMEOUT = 5000; // 5 seconds call timeout. TODO: Configurable?

template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
void Forward(const gen::RpcContext& ctx,
             const REQ& req, RESP& resp, SERVICE_FUNC serviceFunc,
             const std::string& addressUri)
{
    // Copy metadata from ServerContext
    std::map<std::string, std::string> metadata;
    for(const auto& pair : ctx.GetServerContext()->client_metadata())
    {
//        std::cout << pair.first << "-->" << pair.second << std::endl;
        metadata[std::string(pair.first.data(), pair.first.size())] = std::string(pair.second.data(), pair.second.size());
    }

    // Call Grpc Service
    std::string errMsg;
    gen::GrpcClient<SERVICE> grpcClient(addressUri);
    if(!grpcClient.Call(serviceFunc, req, resp, metadata, errMsg, GRPC_TIMEOUT))
    {
        ctx.SetStatus(::grpc::INTERNAL, errMsg);

        // Note: errMsg already has the request name and the addressUri
        std::cerr << "ERROR: From " << ctx.Peer()
                << ", status=" << gen::StatusToStr(::grpc::INTERNAL)
                << ": " << errMsg << std::endl;
    }
    else
    {
        std::cout << "SUCCESS: From " << ctx.Peer()
                << ", status=" << gen::StatusToStr(::grpc::OK)
                << ", req=" << req.GetTypeName() << ", addressUri='" << addressUri << "'" << std::endl;
    }
}

//
// Helper class to read stream of messages
//
template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
class GrpcStreamReader
{
public:
    GrpcStreamReader() = default;
    ~GrpcStreamReader() { Stop(); }

    void Call(const gen::RpcServerStreamContext& ctx,
              const REQ& req, SERVICE_FUNC serviceFunc, const std::string& addressUri)
    {
        mThread = std::thread([&, serviceFunc]()
        {
            std::function respCallback = [&](const RESP& resp) -> bool
            {
                mPipe.Push(resp);
//                std::cout << "resp=" << resp << std::endl;
                return true;
            };

            gen::GrpcClient<SERVICE> grpcClient(addressUri);
            if(!grpcClient.CallStream(serviceFunc, req, respCallback, mErrMsg))
            {
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

template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
void Forward(const gen::RpcServerStreamContext& ctx,
             const REQ& req, RESP& resp, SERVICE_FUNC serviceFunc,
             const std::string& addressUri)
{
    // Start or continue streaming
    auto reader = (GrpcStreamReader<SERVICE, SERVICE_FUNC, REQ, RESP>*)ctx.GetParam();

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
            reader = new (std::nothrow) GrpcStreamReader<SERVICE, SERVICE_FUNC, REQ, RESP>;
            reader->Call(ctx, req, serviceFunc, addressUri);
            ctx.SetParam(reader);
        }

        // Get data to send
        if(reader->Read(resp))
        {
            ctx.SetHasMore(true); // Ask for more data to send
        }
        else if(!reader->IsValid())
        {
            ctx.SetStatus(reader->GetStatus(), reader->GetError());
            ctx.SetHasMore(false); // No more data to send

            // Note: Reader's error already has the request name and the addressUri
            std::cerr << "ERROR: From " << ctx.Peer()
                    << ", status=" << gen::StatusToStr(reader->GetStatus())
                    << ": " << reader->GetError() << std::endl;
        }
        else
        {
            ctx.SetHasMore(false); // No more data to send

            std::cout << "SUCCESS: From " << ctx.Peer()
                    << ", status=" << gen::StatusToStr(reader->GetStatus())
                    << ", req=" << req.GetTypeName() << ", addressUri='" << addressUri << "'" << std::endl;
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
//            std::cout << "SUCCESS: status=" << gen::StatusToStr(reader->GetStatus())
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

template <class SERVICE, class SERVICE_FUNC, class REQ, class RESP>
void Forward(const gen::RpcClientStreamContext& ctx,
             const REQ& req, RESP& resp, SERVICE_FUNC serviceFunc,
             const std::string& addressUri)
{
    ctx.SetStatus(::grpc::INTERNAL, "Not Implemented Yet");
}

#endif // __PROXY_UTILS_HPP__


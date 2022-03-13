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
class GrpcClient
{
public:
    GrpcClient() = default;
    ~GrpcClient() = default;

    GrpcClient(const std::string& domainSocketPath,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        Init(domainSocketPath, creds);
    }

    GrpcClient(const std::string& host, unsigned short port,
               const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        Init(host, port, creds);
    }

    // Do not allow copy constructor and assignment operator (prevent class copy)
    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    // To call the server, we need to instantiate a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint specified by addressUri.
    // Note: The channel isn't authenticated be default (use of InsecureChannelCredentials()).

    void Init(const std::string& host, unsigned short port,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        addressUri = FormatDnsAddressUri(host.c_str(), port);
        channel = grpc::CreateChannel(addressUri, (creds ? creds : grpc::InsecureChannelCredentials()));
    }

    void Init(const std::string& domainSocketPath,
              const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        addressUri = FormatUnixDomainSocketAddressUri(domainSocketPath.c_str());
        channel = grpc::CreateChannel(addressUri, (creds ? creds : grpc::InsecureChannelCredentials()));
    }

    void InitFromAddressUri(const std::string& addressUriIn,
                            const std::shared_ptr<grpc::ChannelCredentials>& creds = nullptr)
    {
        addressUri = addressUriIn;
        channel = grpc::CreateChannel(addressUri, (creds ? creds : grpc::InsecureChannelCredentials()));
    }

    // Thread-save UNARY gRpc
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallMT(GRPC_STUB_FUNC grpcStubFunc,
                const REQ& req, RESP& resp,
                const std::map<std::string, std::string>& metadata,
                std::string& errMsg)
    {
        // Create stub
        std::unique_ptr<typename RPC_SERVICE::Stub> stub = RPC_SERVICE::NewStub(channel);
        if(!stub.get())
        {
            SetError(errMsg, "Failed to create gRpc service stub");
            return false;
        }

        // Create context and set metadata (if we have any...)
        grpc::ClientContext context;
        for(const std::pair<std::string, std::string>& p : metadata)
            context.AddMetadata(p.first, p.second);

        // Call service
        grpc::Status s = (stub.get()->*grpcStubFunc)(&context, req, &resp);
        if(!s.ok())
        {
            SetError(errMsg, "Failed to make unary call", s);
            return false;
        }

        return true;
    }

    // Thread-save UNARY gRpc - no metadata
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallMT(GRPC_STUB_FUNC grpcStubFunc,
                const REQ& req, RESP& resp,
                std::string& errMsg)
    {
        return CallMT<RPC_SERVICE>(grpcStubFunc, req, resp, dummy_metadata, errMsg);
    }

    // Thread-save server-side STREAM gRpc
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                      const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                      const std::map<std::string, std::string>& metadata,
                      std::string& errMsg)
    {
        // Create stub
        std::unique_ptr<typename RPC_SERVICE::Stub> stub = RPC_SERVICE::NewStub(channel);
        if(!stub.get())
        {
            SetError(errMsg, "Failed to create gRpc service stub");
            return false;
        }

        // Create context and set metadata (if we have any...)
        grpc::ClientContext context;
        for(const std::pair<std::string, std::string>& p : metadata)
            context.AddMetadata(p.first, p.second);

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

    // Thread-save server-side STREAM gRpc - no metadata
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                      const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                      std::string& errMsg)
    {
        return CallStreamMT<RPC_SERVICE>(grpcStubFunc, req, respCallback, dummy_metadata, errMsg);
    }

    // Thread-save client-side STREAM gRpc
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                            ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                            const std::map<std::string, std::string>& metadata,
                            std::string& errMsg)
    {
        // Create stub
        std::unique_ptr<typename RPC_SERVICE::Stub> stub = RPC_SERVICE::NewStub(channel);
        if(!stub.get())
        {
            SetError(errMsg, "Failed to create gRpc service stub");
            return false;
        }

        // Create context and set metadata (if we have any...)
        grpc::ClientContext context;
        for(const std::pair<std::string, std::string>& p : metadata)
            context.AddMetadata(p.first, p.second);

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

    // Thread-save client-side STREAM gRpc - no metadata
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStreamMT(GRPC_STUB_FUNC grpcStubFunc,
                            ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                            std::string& errMsg)
    {
        return CallClientStreamMT<RPC_SERVICE>(grpcStubFunc, reqCallback, resp, dummy_metadata, errMsg);
    }

    //
    // Single threaded versions.
    // Note: The only difference versus multy-threaded versions is that
    // errMsg is not a function argument but a class member. The error
    // can be obtained after the call by calling GetError().
    //

    // Single-thread UNARY gRpc (to be used in single-threaded application)
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool Call(GRPC_STUB_FUNC grpcStubFunc,
              const REQ& req, RESP& resp,
              const std::map<std::string, std::string>& metadata = dummy_metadata)
    {
        errMsg.clear();
        return CallMT<RPC_SERVICE>(grpcStubFunc, req, resp, metadata, errMsg);
    }

    // Single-thread server-side STREAM gRpc (to be used in single-threaded application)
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallStream(GRPC_STUB_FUNC grpcStubFunc,
                    const REQ& req, RespCallbackFunctor<RESP>& respCallback,
                    const std::map<std::string, std::string>& metadata = dummy_metadata)
    {
        errMsg.clear();
        return CallStreamMT<RPC_SERVICE>(grpcStubFunc, req, respCallback, metadata, errMsg);
    }

    // Single-thread client-side STREAM gRpc (to be used in single-threaded application)
    template <class RPC_SERVICE, class GRPC_STUB_FUNC, class REQ, class RESP>
    bool CallClientStream(GRPC_STUB_FUNC grpcStubFunc,
                          ReqCallbackFunctor<REQ>& reqCallback, RESP& resp,
                          const std::map<std::string, std::string>& metadata = dummy_metadata)
    {
        errMsg.clear();
        return CallClientStreamMT<RPC_SERVICE>(grpcStubFunc, reqCallback, resp, metadata, errMsg);
    }

    const std::string GetAddressUri() { return addressUri; }
    bool IsValid() { return !addressUri.empty(); }

    // Note: GetError() is only valid when using with single-threaded calls
    const std::string GetError() { return errMsg; }

private:
    void SetError(std::string& errMsg, const std::string& err, const grpc::Status& status)
    {
        std::string status_err = status.error_message();
        if(status_err.empty())
            status_err = StatusToStr(status.error_code());
        errMsg = err + ", addressUri='" + addressUri + "', err='" + status_err + "'";
    }

    void SetError(std::string& errMsg, const std::string& err)
    {
        errMsg = err + ", addressUri='" + addressUri + "'";
    }

private:
    std::shared_ptr<grpc::Channel> channel;
    std::string addressUri;
    std::string errMsg;     // Only used by single-threaded calls

    // Dummy metadata for no-metadata calls
    static inline const std::map<std::string, std::string> dummy_metadata;
};

} //namespace gen

#endif // __GRPC_CLIENT_HPP__
// *INDENT-ON*


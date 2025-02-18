//
// intercepor.hpp
//
#ifndef __INTERCEPTOR_HPP__
#define __INTERCEPTOR_HPP__

#include "grpcContext.hpp"      // gen::RpcContext
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/server_interceptor.h>

//
// Experimental: Interceptor class
//
class MyInterceptor : public grpc::experimental::Interceptor 
{
public:
    MyInterceptor(grpc::experimental::ServerRpcInfo* info) : rpcInfo(info) {}

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override 
    {
        if(methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) 
        {
            grpc::ServerContextBase* ctx = rpcInfo->server_context();
            // auto& metadata = srvCtx->client_metadata();
            // std::string peer = srvCtx->peer();
            // void* msg = methods->GetRecvMessage();

            std::string sessionId = GetMetadata(ctx, "sessionid");
            std::string requestId = GetMetadata(ctx, "requestid");

            OUTMSG("method='" << rpcInfo->method() << "'" <<
                   ", sessionId='" << sessionId << "'" <<
                   ", requestId='" << requestId << "'");

            if(std::string_view(rpcInfo->method()) == "/test.Hello/Ping")
            {
                // Do something for Ping message
            }
        }

        methods->Proceed();
    }

private:
    std::string GetMetadata(grpc::ServerContextBase* ctx, const char* key)
    {
        const auto& metadata = ctx->client_metadata();
        if(auto itr = metadata.find(key); itr != metadata.end())
            return std::string(itr->second.data(), itr->second.size());
        else
            return "";
    }

    grpc::experimental::ServerRpcInfo* rpcInfo{nullptr};
};

class MyInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface 
{
public:
    grpc::experimental::Interceptor* CreateServerInterceptor(grpc::experimental::ServerRpcInfo* info) override 
    {
        return new MyInterceptor(info);
    }
};

#endif // __INTERCEPTOR_HPP__


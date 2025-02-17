//
// intercepor.hpp
//
#ifndef __INTERCEPTOR_HPP__
#define __INTERCEPTOR_HPP__

#include "grpcContext.hpp"      // gen::RpcContext
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
            // std::cout << "Got a new RPC " << rpcInfo->method() << ": POST_RECV_INITIAL_METADATA" << std::endl;

            if(std::string_view(rpcInfo->method()) == "/test.Hello/Ping")
            {
                //if(!Authenticate())
                //    methods->Hijack();
            }
        }

        methods->Proceed();
    }

private:
    bool Authenticate()
    {
        grpc::ServerContextBase* ctx = rpcInfo->server_context();
        // auto& metadata = srvCtx->client_metadata();
        // std::string peer = srvCtx->peer();
        // void* msg = methods->GetRecvMessage();

        std::string sessionId;
        std::string requestId;

        gen::Context::GetMetadata(ctx, "sessionid", sessionId);
        gen::Context::GetMetadata(ctx, "requestid", requestId);

        std::cout << "sessionid='" << sessionId << "'" << std::endl;
        std::cout << "requestid='" << requestId << "'" << std::endl;

        // TODO
        return false;
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


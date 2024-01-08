#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include "grpcServer.hpp"
#include "helloService.hpp"
#include "healthService.hpp"
#include <atomic>

class MyServer : public gen::GrpcServer
{
public:
    MyServer() : helloService(this) {}
    virtual ~MyServer() = default;

    // gen::GrpcServer overrides
    virtual bool OnInit(::grpc::ServerBuilder& builder) override;
    virtual bool OnRun() override;
    virtual void OnError(const std::string& err) const override;
    virtual void OnInfo(const std::string& info) const override;

    bool Shutdown(const gen::RpcContext& ctx, std::string& errMsg);

private:
    std::atomic<bool> mStop{false};

    HelloService helloService;
    HealthService healthService;
};

#endif // __SERVER_HPP__

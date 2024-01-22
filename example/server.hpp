#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include "grpcServer.hpp"
#include "helloService.hpp"
#include "healthService.hpp"
#include <atomic>

class MyServer : public gen::GrpcServer
{
public:
    MyServer()
    {
        // Add all services
        AddService<HelloService>();
        AddService<HealthService>();
    }
    virtual ~MyServer() = default;

private:
    // gen::GrpcServer overrides
    virtual bool OnInit(::grpc::ServerBuilder& builder) override;
    virtual void OnRun() override;
    virtual void OnError(const std::string& err) const override;
    virtual void OnInfo(const std::string& info) const override;
};

#endif // __SERVER_HPP__

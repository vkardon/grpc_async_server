//
// healthService.hpp
//
#ifndef __HEALTH_SERVICE_HPP__
#define __HEALTH_SERVICE_HPP__

#include "grpcContext.hpp"

namespace grpc { namespace health { namespace v1
{
class HealthCheckRequest;
class HealthCheckResponse;
}}}

class HealthService : public gen::GrpcService
{
public:
    HealthService() = default;
    virtual ~HealthService() = default;

    virtual bool Init(gen::GrpcServer* srv) override;

    void Check(const gen::RpcContext& rpcCtx,
               const grpc::health::v1::HealthCheckRequest& req,
               grpc::health::v1::HealthCheckResponse& resp);

private:
    gen::GrpcServer* srv_{nullptr};
};

#endif // __HEALTH_SERVICE_HPP__


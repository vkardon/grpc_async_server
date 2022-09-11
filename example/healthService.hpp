//
// healthService.hpp
//
#ifndef __HEALTH_SERVICE_HPP__
#define __HEALTH_SERVICE_HPP__

#include "grpcServer.hpp"
#include "health.grpc.pb.h"

class HealthService : public gen::GrpcService<grpc::health::v1::Health>
{
public:
    HealthService() = default;
    virtual ~HealthService() = default;

    virtual bool Init() override;

    void Check(const gen::RpcContext& rpcCtx,
               const grpc::health::v1::HealthCheckRequest& req,
               grpc::health::v1::HealthCheckResponse& resp);
};

#endif // __HEALTH_SERVICE_HPP__


//
// healthService.cpp
//
#include "healthService.hpp"

bool HealthService::OnInit()
{
    // Add Health service RPCs
    Bind(&HealthService::Check,
         &grpc::health::v1::Health::AsyncService::RequestCheck);

    return true;
}

void HealthService::Check(const gen::RpcContext& rpcCtx,
                          const grpc::health::v1::HealthCheckRequest& req,
                          grpc::health::v1::HealthCheckResponse& resp)
{
    ::grpc::StatusCode statusCode = ::grpc::OK; // Initially
    std::string errMsg;

    const std::string& serviceName = req.service();
    if(serviceName.empty())
    {
        // If client is not interested in a specific service, then return
        // server's overall health status
        resp.set_status(grpc::health::v1::HealthCheckResponse::SERVING);
    }
    else
    {
        // Return service-specific health status
        gen::GrpcServiceBase* service = srv->GetService(serviceName);
        if(!service)
        {
            statusCode = ::grpc::NOT_FOUND;
            errMsg = "The service '" + serviceName + "' is unknown";
        }
        else
        {
            resp.set_status(service->IsServing() ?
                            grpc::health::v1::HealthCheckResponse::SERVING :
                            grpc::health::v1::HealthCheckResponse::NOT_SERVING);
        }
    }

    rpcCtx.SetStatus(statusCode, errMsg);
}


//
// controlService.hpp
//
#ifndef __CONTROL_SERVICE_HPP__
#define __CONTROL_SERVICE_HPP__

#include "grpcServer.hpp"
#include "control.grpc.pb.h"

class ControlService : public gen::GrpcService<test::Control>
{
public:
    ControlService() = default;
    virtual ~ControlService() = default;

private:
    // gen::GrpcService overrides
    virtual bool OnInit() override
    {
        // Bind all ControlService RPCs
        Bind(&ControlService::Shutdown, &test::Control::AsyncService::RequestShutdown);
        Bind(&ControlService::Status, &test::Control::AsyncService::RequestStatus);
        return true;
    }

    // Supported RPCs
    void Shutdown(const gen::RpcContext& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp)
    {
        srv->Shutdown();
        resp.set_result(true);
    }

    void Status(const gen::RpcContext& ctx,
                const test::StatusRequest& req, test::StatusResponse& resp)
    {
        const std::string& serviceName = req.service_name();
        std::string serviceStatus;

        if(serviceName.empty())
        {
            serviceStatus = "Invalid (empty) service name";
        }
        else if(auto service = srv->GetService(serviceName); service)
        {
            serviceStatus = "The service '" + serviceName + "' is available";
        }
        else
        {
            serviceStatus = "The service '" + serviceName + "' is unknown";
        }

        resp.set_service_status(serviceStatus);
    }
};

#endif // __CONTROL_SERVICE_HPP__


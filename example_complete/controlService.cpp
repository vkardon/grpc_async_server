//
// controlService.cpp
//
#include "controlService.hpp"
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG

void ControlService::Shutdown(const gen::RpcContext& ctx,
                            const test::ShutdownRequest& req,
                            test::ShutdownResponse& resp)
{
    // Get client IP address
    std::string clientAddr = ctx.Peer();

    // Check if this request is from a local host
    if(gen::IsLocalhost(clientAddr))
    {
        INFOMSG("From the local client " << clientAddr);
        srv->Shutdown();
        resp.set_result(true);
    }
    else
    {
        INFOMSG("From the remote client " << clientAddr << ": Remote shutdown is not allowed");
        resp.set_result(false);
        resp.set_msg("Shutdown from a remote client is not allowed");
    }
}

void ControlService::Status(const gen::RpcContext& ctx,
                            const test::StatusRequest& req,
                            test::StatusResponse& resp)
{
    INFOMSG("From " << ctx.Peer());

    const std::string& serviceName = req.service_name();
    std::string serviceStatus;

    if(serviceName.empty())
    {
        serviceStatus = "Invalid (empty) service name";
    }
    else
    {
        // Return service-specific health status
        gen::GrpcServiceBase* service = srv->GetService(serviceName);
        if(!service)
        {
            serviceStatus = "The service '" + serviceName + "' is unknown";
        }
        else if(service->IsServing())
        {
            serviceStatus = "The service '" + serviceName + "' is serving";
        }
        else
        {
            serviceStatus = "The service '" + serviceName + "' is NOT serving";
        }
    }

    resp.set_service_status(serviceStatus);
}

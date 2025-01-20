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

    // gen::GrpcService overrides
    virtual bool OnInit() override
    {
        // Bind all ControlService RPCs
        Bind(&ControlService::Shutdown, &ControlService::RequestShutdown);
        Bind(&ControlService::Status, &ControlService::RequestStatus);
        return true;
    }

    // You can override IsServing for some service-specific check
    // if service is serving.
    //virtual bool IsServing() override
    //{
    //    return true;
    //}

protected:
    // Supported RPCs
    void Shutdown(const gen::RpcContext& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp);
    
    void Status(const gen::RpcContext& ctx,
                const test::StatusRequest& req, test::StatusResponse& resp);
};

#endif // __CONTROL_SERVICE_HPP__


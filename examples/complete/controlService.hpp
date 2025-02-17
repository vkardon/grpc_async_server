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
    void Shutdown(const gen::Context& ctx,
                  const test::ShutdownRequest& req, test::ShutdownResponse& resp);
    
    void Status(const gen::Context& ctx,
                const test::StatusRequest& req, test::StatusResponse& resp);
};

#endif // __CONTROL_SERVICE_HPP__


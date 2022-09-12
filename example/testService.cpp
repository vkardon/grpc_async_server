//D
// testService.cpp
//
#include "testService.hpp"
#include "testServer.hpp"
#include "logger.hpp"

bool TestService::Init()
{
    // Add TestService RPCs
    AddRpc(&TestService::Shutdown,
            &test::GrpcService::AsyncService::RequestShutdown);
    AddRpc(&TestService::Ping,
            &test::GrpcService::AsyncService::RequestPing);
    AddRpc(&TestService::ServerStreamTest,
            &test::GrpcService::AsyncService::RequestServerStreamTest);
    AddRpc(&TestService::ClientStreamTest,
            &test::GrpcService::AsyncService::RequestClientStreamTest);

    return true;
}

bool TestService::IsServing()
{
    // Add some service-specific code to determine
    // is the service is serving or not
    return true;
}

void TestService::Shutdown(const gen::RpcContext& ctx,
                           const test::ShutdownRequest& req, test::ShutdownResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx); // Set thread-specific logger prefix

    std::string errMsg;

    if(mServer->Shutdown(ctx, errMsg))
    {
        resp.set_result(true);
    }
    else
    {
        resp.set_result(false);
        resp.set_msg(errMsg);
    }
}

void TestService::Ping(const gen::RpcContext& ctx,
                       const test::PingRequest& req, test::PingResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx); // Set thread-specific logger prefix

    resp.set_result(true);

    INFOMSG_MT("From " << ctx.Peer());
}

void TestService::ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                                   const test::ServerStreamTestRequest& req, test::ServerStreamTestResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx); // Set thread-specific logger prefix

    // Statistics - track the total number of opened streams
    static std::atomic<int> opened_streams{0};

    struct ResponseList
    {
        std::vector<std::string> rows;
        size_t sentRows = 0;
    };

    ResponseList* respList = (ResponseList*)ctx.GetParam();

    // Are we done?
    if(ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ||
            ctx.GetStreamStatus() == gen::StreamStatus::ERROR)
    {
        OUTMSG_MT((ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ? "SUCCESS" : "ERROR")
                  << ", stream=" << ctx.GetParam()
                  << ", sent "  << (respList ? respList->sentRows  : 0)
                  << " out of " << (respList ? respList->rows.size() : 0) << " rows");

        // Clean up...
        if(respList)
            delete respList;

        respList = nullptr;
        ctx.SetParam(nullptr);
        opened_streams--;   // The total number of opened streams
    }
    else
    {
        // Start or continue streaming
        // (ctx.GetStreamStatus() == gen::StreamStatus::STREAMING)
        if(!respList)
        {
            // This is very first response, create list of empty responses to be streammed
            OUTMSG_MT("Req = '" << req.msg() << "'");

            respList = new ResponseList;
            ctx.SetParam(respList);
            opened_streams++;   // The total number of opened streams

            // Initialize responses with some data
            for(size_t i = 0; i < 10; ++i)
                respList->rows.push_back("ServerStreamTestResponse #" + std::to_string(i+1));
        }

        // Get rows to send
        if(respList->sentRows < respList->rows.size())
        {
            // Get the next row to send
            std::stringstream ss;
            ss << "Resp[" << (respList->sentRows + 1) << "]: '" << respList->rows[respList->sentRows] << "'";
            resp.set_msg(ss.str());
            resp.set_result(true);
            respList->sentRows++;
            ctx.SetHasMore(true); // Ask for more data to send
        }
        else
        {
            ctx.SetHasMore(false); // No more rows to send
        }
    }

    //usleep(500000);   // sleep for half second to simulate processing
    //usleep(1000000);  // sleep for one second to simulate processing

    // Print statistics.
    OUTMSG_MT("opened_streams=" << opened_streams);
}

void TestService::ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                                   const test::ClientStreamTestRequest& req, test::ClientStreamTestResponse& resp)
{
//    static int count = 0;
//    if(++count %12 == 0)
//    {
//        std::cout << ">>> " << __func__ << " cancel processing" << std::endl;
//
//        ctx.SetStatus(::grpc::CANCELLED, "");
//        return;
//    }

    if(ctx.GetHasMore())
    {
        INFOMSG_MT("req.msg='" << req.msg() << "'");
    }
    else
    {
        // Done with client-stream reading. Write response back
        // ...
        resp.set_result(true);
    }
}



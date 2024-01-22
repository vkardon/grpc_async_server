//
// helloService.cpp
//
#include "helloService.hpp"
#include "serverConfig.hpp"  // OUTMSG, INFOMSG, ERRORMSG

bool HelloService::Init()
{
    // Add HelloService RPCs
    Bind(&HelloService::Shutdown,
         &test::Hello::AsyncService::RequestShutdown);
    Bind(&HelloService::Ping,
         &test::Hello::AsyncService::RequestPing);
    Bind(&HelloService::ServerStreamTest,
         &test::Hello::AsyncService::RequestServerStreamTest);
    Bind(&HelloService::ClientStreamTest,
         &test::Hello::AsyncService::RequestClientStreamTest);

    return true;
}

bool HelloService::IsServing()
{
    // Add some service-specific code to determine
    // is the service is serving or not
    return true;
}

void HelloService::Shutdown(const gen::RpcContext& ctx,
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

void HelloService::Ping(const gen::RpcContext& ctx,
                        const test::PingRequest& req,
                        test::PingResponse& resp)
{
    resp.set_result(true);

    INFOMSG("From " << ctx.Peer());
}

void HelloService::ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                                    const test::ServerStreamTestRequest& req,
                                    test::ServerStreamTestResponse& resp)
{
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
        OUTMSG((ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ? "SUCCESS" : "ERROR")
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
            OUTMSG("Req = '" << req.msg() << "'");

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
    OUTMSG("opened_streams=" << opened_streams);
}

void HelloService::ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                                    const test::ClientStreamTestRequest& req,
                                    test::ClientStreamTestResponse& resp)
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
        INFOMSG("req.msg='" << req.msg() << "'");
    }
    else
    {
        // Done with client-stream reading. Write response back
        // ...
        resp.set_result(true);
    }
}



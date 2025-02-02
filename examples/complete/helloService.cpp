//
// helloService.cpp
//
#include "helloService.hpp"
#include "logger.hpp"           // OUTMSG, INFOMSG, ERRORMSG, etc.

// ResponseList is used to demonstrate server-side streaming
struct ResponseList
{
    std::vector<std::string> rows;
    size_t sentRows = 0;

    ResponseList()
    {
        // Initialize responses with some data
        for(size_t i = 0; i < 10; ++i)
            rows.push_back("ReponseList row #" + std::to_string(i+1));
    }
};

void HelloService::PingTest(const gen::RpcContext& ctx,
                            const test::PingRequest& req,
                            test::PingResponse& resp)
{
    INFOMSG("From " << ctx.Peer());

    resp.set_msg("Pong");
}

void HelloService::ServerStreamTest(const gen::RpcServerStreamContext& ctx,
                                    const test::ServerStreamRequest& req,
                                    test::ServerStreamResponse& resp)
{
//    // Statistics - track the total number of opened streams
//    static std::atomic<int> opened_streams{0};

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
//        opened_streams--;   // Statistics: The total number of opened streams
    }
    else
    {
        // Start or continue streaming
        // (ctx.GetStreamStatus() == gen::StreamStatus::STREAMING)
        if(!respList)
        {
            // This is very first response.
            OUTMSG("Req = '" << req.msg() << "'");

            // Initialize some data to stream back to the client
            respList = new ResponseList;
            ctx.SetParam(respList);
//            opened_streams++;   // Statistics: The total number of opened streams
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

//    // Print statistics.
//    OUTMSG("opened_streams=" << opened_streams);
}

void HelloService::ClientStreamTest(const gen::RpcClientStreamContext& ctx,
                                    const test::ClientStreamRequest& req,
                                    test::ClientStreamResponse& resp)
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
        INFOMSG(req);
    }
    else
    {
        // Done with client-stream reading. Write response back
        // ...
        resp.set_result(true);
    }
}



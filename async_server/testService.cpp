//
// testService.cpp
//
#include "testService.h"
#include "testServer.h"
#include "logger.h"
#include "test.grpc.pb.h"

//
// Helper macros for Session Manager service
//
#define TEST_UNARY(RPC) ADD_UNARY( \
    RPC, test::RPC##Request, test::RPC##Response, test::GrpcService, \
    &TestService::RPC, srv)

#define TEST_STREAM(RPC) ADD_STREAM( \
    RPC, test::RPC##Request, test::RPC##Response, test::GrpcService, \
    &TestService::RPC, srv)

bool TestService::Init(gen::GrpcServer* srv)
{
    // Add TestService RPCs
    TEST_UNARY(Shutdown)
    TEST_UNARY(Ping)
    TEST_STREAM(StreamTest)
    return true;
}

void TestService::Shutdown(const gen::RpcContext& ctx,
        const test::ShutdownRequest& req, test::ShutdownResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx); // Set thread-specific logger prefix

    std::string errMsg;

    if(mServer->Shutdown(ctx, errMsg))
    {
        test::Result* result = resp.mutable_result();
        result->set_status(test::Result::SUCCESS);
    }
    else
    {
        test::Result* result = resp.mutable_result();
        result->set_status(test::Result::ERROR);
        result->set_message(errMsg);
    }

    ctx.SetStatus(::grpc::OK, "");
}

void TestService::Ping(const gen::RpcContext& ctx,
        const test::PingRequest& req, test::PingResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx); // Set thread-specific logger prefix

    test::Result* result = resp.mutable_result();
    result->set_status(test::Result::SUCCESS);

    INFOMSG_MT("From " << ctx.Peer());

    ctx.SetStatus(::grpc::OK, "");
}

void TestService::StreamTest(const gen::RpcStreamContext ctx,
        const test::StreamTestRequest& req, test::StreamTestResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx); // Set thread-specific logger prefix

    // Statistics - track the total number of opened streams
    static std::atomic<int> opened_streams{0};

    struct ResponseList
    {
        std::vector<std::string> rows;
        size_t totalRows = 0;   // Only used for logging
        size_t sentRows = 0;    // Only used for logging
        size_t pendingRows = 0; // Only used for logging
    };

    ResponseList* respList = (ResponseList*)ctx.GetParam();

    // Are we done?
    if(ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ||
       ctx.GetStreamStatus() == gen::StreamStatus::ERROR)
    {
        OUTMSG_MT((ctx.GetStreamStatus() == gen::StreamStatus::SUCCESS ? "SUCCESS" : "ERROR")
                << ", stream=" << ctx.GetParam()
                << ", sent "  << (respList ? respList->sentRows  : 0)
                << " out of " << (respList ? respList->totalRows : 0) << " rows");

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
            respList = new ResponseList;
            respList->totalRows = 10; // Stream 10 messages
            ctx.SetParam(respList);
            opened_streams++;   // The total number of opened streams

            // Initialize responses with some data
            respList->rows.resize(respList->totalRows);
            for(size_t i = 0; i < respList->rows.size(); ++i)
                respList->rows[i] = "Row" + std::to_string(i+1) + " Hello From Grpc Server";
        }

        // Get rows to send
        respList->sentRows += respList->pendingRows; // From the previous send
        respList->pendingRows = 0;

        if(respList->sentRows < respList->rows.size())
        {
            // Get the next row to send
            std::stringstream ss;
            ss << "[" << req.msg() << "] Resp Order: " << (respList->sentRows + 1) << ", "
               << "Data: '" << respList->rows[respList->sentRows] << "'";
            resp.set_msg(ss.str());
            respList->pendingRows = 1;
        }

        // Do we have any rows to send?
        ctx.SetHasMore(respList->pendingRows > 0 ? true : false);
    }

    //usleep(500000);   // sleep for half second to simulate processing
    //usleep(1000000);  // sleep for one second to simulate processing

    // Print statistics.
    OUTMSG_MT("opened_streams=" << opened_streams);

    ctx.SetStatus(::grpc::OK, "");
}


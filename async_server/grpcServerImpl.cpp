//
// grpcServerImpl.cpp
//
#include "grpcServerImpl.h"
#include "logger.h"
#include "grpcServer.grpc.pb.h"
#include <unistd.h> // usleep

//
// GrpcServerImpl:Context implementation
//
const char* METADATA_SESSION_ID = "sessionid";
const char* METADATA_REQUEST_ID = "requestid";

GrpcServerImpl::Context::Context(grpc::ServerContext* ctx) : mCtx(ctx)
{
    // Set thread-specific logging prefix "[session id][request id]"
    std::string sessionId;
    std::string requestId;

    GetMetadata(METADATA_SESSION_ID, sessionId);
    GetMetadata(METADATA_REQUEST_ID, requestId);

    char prefix[256]{};
    char* ptr = prefix;
    if(!sessionId.empty())
        ptr += sprintf(ptr, "[sid=%s]", sessionId.c_str());
    if(!requestId.empty())
        ptr += sprintf(ptr, "[rid=%s]", requestId.c_str());
    *ptr++ = ' ';
    *ptr = '\0';

    mLoggerPrefix = prefix;
}

void GrpcServerImpl::Context::GetMetadata(const char* key, std::string& value)
{
    const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata = mCtx->client_metadata();
    auto itr = client_metadata.find(key);
    if(itr != client_metadata.end())
        value.assign(itr->second.data(), itr->second.size());
}

//
// Helper class to set thread-specific logger prefix
//
struct LoggerPrefix
{
    LoggerPrefix(const GrpcServerImpl::Context& ctx)
        { Logger::SetThreadPrefix(ctx.mLoggerPrefix.c_str()); }
    ~LoggerPrefix() { Logger::SetThreadPrefix(""); }
};

//
// GrpcServerImpl implementation
//

// Return true to keep running, false to shutdown
bool GrpcServerImpl::OnRun()
{
    if(mStop)
        return false; // We are done

    return true;
}

void GrpcServerImpl::Shutdown(const Context& ctx,
        const ShutdownRequest& req, ShutdownResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx);
    Result* result = resp.mutable_result();

    // Get client IP addr
    std::string clientAddr = ctx.mCtx->peer();

    // Check if this request is from a local host
    // Note: Based on grpc_1.0.0/test/cpp/end2end/end2end_test.cc
    const std::string kIpv6("ipv6:[::1]:");
    const std::string kIpv4MappedIpv6("ipv6:[::ffff:127.0.0.1]:");
    const std::string kIpv4("ipv4:127.0.0.1:");

    bool isLocalHost = (clientAddr.substr(0, kIpv4.size()) == kIpv4 ||
                        clientAddr.substr(0, kIpv4MappedIpv6.size()) == kIpv4MappedIpv6 ||
                        clientAddr.substr(0, kIpv6.size()) == kIpv6);

    if(isLocalHost)
    {
        INFOMSG_MT("From local client " << clientAddr << ", Reason='" << req.reason() << "'");
        result->set_status(Result::SUCCESS);
        mStop = true; // Force OnRun() to return false
    }
    else
    {
        INFOMSG_MT("From remote client " << clientAddr << ": remote shutdown is not allowed");
        result->set_status(Result::ERROR);
        result->set_message("Shutdown from remote client is not allowed");
    }
}

void GrpcServerImpl::Ping(const Context& ctx,
        const PingRequest& /*req*/, PingResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx);
    Result* result = resp.mutable_result();

    INFOMSG_MT("From " << ctx.mCtx->peer());

    result->set_status(Result::SUCCESS);
}

void GrpcServerImpl::StreamTest(const StreamContext& ctx,
        const StreamTestRequest& req, StreamTestResponse& resp)
{
    LoggerPrefix loggerPrefix(ctx);

    // Statistics - track the total number of opened streams
    static std::atomic<int> opened_streams{0};

    struct ResponseList
    {
        std::vector<std::string> rows;
        size_t totalRows = 0;   // Only used for logging
        size_t sentRows = 0;    // Only used for logging
        size_t pendingRows = 0; // Only used for logging
    };

    ResponseList* respList = (ResponseList*)ctx.mStream;

    // Are we done?
    if(ctx.mStatus == StreamContext::SUCCESS ||
       ctx.mStatus == StreamContext::ERROR)
    {
        OUTMSG_MT((ctx.mStatus == StreamContext::SUCCESS ? "SUCCESS" : "ERROR")
                << ", stream='" << ctx.mStream << "'"
                << ", sent "  << (respList ? respList->sentRows  : 0)
                << " out of " << (respList ? respList->totalRows : 0) << " rows");

        // Clean up...
        if(respList)
            delete respList;

        respList = nullptr;
        ctx.mStream = nullptr;
        opened_streams--;   // The total number of opened streams
    }
    else
    {
        // Start or continue streaming
        // (ctx.mStatus == StreamContext::STREAMING)
        if(!respList)
        {
            // This is very first response, create list of empty responses to be streammed
            respList = new ResponseList;
            respList->totalRows = 10; // Stream 10 messages
            ctx.mStream = respList;
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
        ctx.mHasMore = (respList->pendingRows > 0 ? true : false);
    }

    //usleep(500000);   // sleep for half second to simulate processing
    //usleep(1000000);  // sleep for one second to simulate processing

    // Print statistics.
    OUTMSG_MT("opened_streams=" << opened_streams);
}



//
// server.cpp
//
#include <grpcpp/grpcpp.h>
#include "hello.grpc.pb.h"

class HelloService final : public test::Hello::Service 
{
    grpc::Status CompressionTest(grpc::ServerContext* context, 
                                 const test::CompressionTestRequest* req, 
                                 test::CompressionTestResponse* resp) override 
    {
        //std::cout << "### " << __func__ << ": compression_algorithm=" << context->compression_algorithm() << std::endl;

        // Overwrite the call's compression algorithm to DEFLATE
        context->set_compression_algorithm(GRPC_COMPRESS_DEFLATE);

        // Populate a respone data with 5KB of 'B'
        resp->set_data(std::string(1024 * 5, 'B'));        
        return grpc::Status::OK;
    }
};

void RunServer() 
{
    std::string server_address("0.0.0.0:50051");

    // Instantiate the server builder
    grpc::ServerBuilder builder;

    // Listen on the given address without any authentication mechanism
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    // Set the default compression algorithm for the server
    builder.SetDefaultCompressionAlgorithm(GRPC_COMPRESS_GZIP);

    // Register *synchronous* "service" to communicate with clients
    HelloService service;
    builder.RegisterService(&service);

    // Finally assemble the server.
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shutdown. 
    // Note: Some other thread must be responsible for shutting down
    // the server for this call to ever return.
    server->Wait();
}

int main(int argc, char** argv) 
{
    RunServer();
    return 0;
}


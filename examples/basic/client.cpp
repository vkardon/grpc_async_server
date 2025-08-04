//
// client.cpp
//
#include <grpcpp/grpcpp.h>
#include "hello.grpc.pb.h"

class HelloClient 
{
public:
    HelloClient(std::shared_ptr<grpc::Channel> channel) : stub_(test::Hello::NewStub(channel)) {}

    void CallCompressionTest() 
    {
        test::CompressionTestRequest req;
        test::CompressionTestResponse resp;

        // Populate a request data with 2MB of 'A'
        req.set_data(std::string(1024 * 1024 * 2, 'A'));

        // Context for the client. It could be used to convey extra information
        // to the server and/or tweak certain RPC behaviors
        grpc::ClientContext context;

        // Overwrite the call's compression algorithm to DEFLATE
        context.set_compression_algorithm(GRPC_COMPRESS_DEFLATE);

        // Make the actual RPC
        grpc::Status status = stub_->CompressionTest(&context, req, &resp);

        // Act upon its status
        if(!status.ok()) 
        {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<test::Hello::Stub> stub_;
};

int main(int argc, char** argv) 
{
    // Set the default compression algorithm for the channel
    grpc::ChannelArguments args;
    args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);

    // Create the channel
    auto channel = grpc::CreateCustomChannel("localhost:50051", grpc::InsecureChannelCredentials(), args);

    // Instantiate the client and call RPC
    HelloClient client(channel);
    client.CallCompressionTest();

    return 0;
}


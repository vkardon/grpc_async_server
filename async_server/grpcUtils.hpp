// *INDENT-OFF*
//
// grpcUtils.hpp
//
#ifndef __GRPC_UTILS_HPP__
#define __GRPC_UTILS_HPP__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/json_util.h> // Protobuf to/from Json support
#pragma GCC diagnostic pop

#include <fstream>  // std::istream
#include <sstream>  // std::ostringstream

namespace gen {

// Helper to format DNS address uri
inline std::string FormatDnsAddressUri(const std::string& host, unsigned short port)
{
    return ("dns:" + host + ":" + std::to_string(port));
}

// Helper to format Unix Domain Socket address uri
inline std::string FormatDomainSocketAddressUri(const std::string& domainSocketPath)
{
    // For Unix domain sockets (Unix systems only)
    // unix:path
    // unix://absolute_path
    //    path indicates the location of the desired socket.
    //    In the first form, the path may be relative or absolute;
    //    In the second form, the path must be absolute
    //    (i.e., there will actually be three slashes, two prior to the path and another to begin the absolute path).
    //
    return ("unix://" + domainSocketPath);
}

inline std::string FormatAbstractSocketAddressUri(const std::string& abstractSocketPath)
{
    // For Unix domain socket in abstract namespace (Unix systems only)
    // unix-abstract:abstract_path
    //    abstract_path indicates a name in the abstract namespace.
    //    The name has no connection with filesystem pathnames.
    //    No permissions will apply to the socket - any process/user may access the socket.
    //    The underlying implementation of Abstract sockets uses a null byte ('\0') as the first character.
    //    The implementation will prepend this null. Do not include the null in abstract_path.
    //    abstract_path cannot contain null bytes.
    return ("unix-abstract:" + abstractSocketPath);
}

// Helper routine to convert grpc::StatusCode to string
inline const char* StatusToStr(grpc::StatusCode code)
{
    switch(code)
    {
    case grpc::StatusCode::OK:                  return "OK";
    case grpc::StatusCode::CANCELLED:           return "CANCELLED";
    case grpc::StatusCode::UNKNOWN:             return "UNKNOWN";
    case grpc::StatusCode::INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
    case grpc::StatusCode::DEADLINE_EXCEEDED:   return "DEADLINE_EXCEEDED";
    case grpc::StatusCode::NOT_FOUND:           return "NOT_FOUND";
    case grpc::StatusCode::ALREADY_EXISTS:      return "ALREADY_EXISTS";
    case grpc::StatusCode::PERMISSION_DENIED:   return "PERMISSION_DENIED";
    case grpc::StatusCode::UNAUTHENTICATED:     return "UNAUTHENTICATED";
    case grpc::StatusCode::RESOURCE_EXHAUSTED:  return "RESOURCE_EXHAUSTED";
    case grpc::StatusCode::FAILED_PRECONDITION: return "FAILED_PRECONDITION";
    case grpc::StatusCode::ABORTED:             return "ABORTED";
    case grpc::StatusCode::OUT_OF_RANGE:        return "OUT_OF_RANGE";
    case grpc::StatusCode::UNIMPLEMENTED:       return "UNIMPLEMENTED";
    case grpc::StatusCode::INTERNAL:            return "INTERNAL";
    case grpc::StatusCode::UNAVAILABLE:         return "UNAVAILABLE";
    case grpc::StatusCode::DATA_LOSS:           return "DATA_LOSS";
    case grpc::StatusCode::DO_NOT_USE:          return "DO_NOT_USE";
    default:                                    return "INVALID_ERROR_CODE";
    }
}

inline bool IsLocalhost(const std::string& ipAddr)
{
    // Check if this is request from a localhost
    // Note: Based on grpc_1.0.0/test/cpp/end2end/end2end_test.cc
    // Note: Calls over unix domain socket are considered local
    const std::string_view kIpv6("ipv6:[::1]:");
    const std::string_view kIpv4MappedIpv6("ipv6:[::ffff:127.0.0.1]:");
    const std::string_view kIpv4("ipv4:127.0.0.1:");
    const std::string_view kUnix("unix:");
    const std::string_view kUnixAbstract("unix-abstract:");

    bool isLocalHost = (ipAddr.substr(0, kIpv4.size()) == kIpv4 ||
                        ipAddr.substr(0, kIpv4MappedIpv6.size()) == kIpv4MappedIpv6 ||
                        ipAddr.substr(0, kIpv6.size()) == kIpv6 ||
                        ipAddr.substr(0, kUnix.size()) == kUnix ||
                        ipAddr.substr(0, kUnixAbstract.size()) == kUnixAbstract);

    return isLocalHost;
}

// Build SSL/TLS Channel and Server credentials
inline bool LoadFile(const std::string& fileName, std::string& buf, std::string& errMsg)
{
    if(fileName.empty())
        return true; // Nothing to load (not an error)

    std::ifstream input(fileName);
    if(!input)
    {
        errMsg = std::string("Failed to load '") + fileName + "': " + strerror(errno);
        return false;
    }

    std::ostringstream ss;
    ss << input.rdbuf();
    buf = std::move(ss.str());
    return true;
}

// Build channel credentials
inline std::shared_ptr<grpc::ChannelCredentials> GetChannelCredentials(
        const std::string& rootCerts,
        const std::string& privateKey,
        const std::string& certChain,
        std::string& errMsg)
{
    grpc::SslCredentialsOptions sslOpts;

    if(!LoadFile(rootCerts, sslOpts.pem_root_certs, errMsg))
        return nullptr;

    if(!LoadFile(privateKey, sslOpts.pem_private_key, errMsg))
        return nullptr;

    if(!LoadFile(certChain, sslOpts.pem_cert_chain, errMsg))
        return nullptr;

    return grpc::SslCredentials(sslOpts);
}

// Build server credentials
inline std::shared_ptr<grpc::ServerCredentials> GetServerCredentials(
        const std::string& rootCerts,
        const std::vector<std::pair<std::string, std::string>>& keyCertPairs,
        grpc_ssl_client_certificate_request_type requestType,
        std::string& errMsg)
{
    grpc::SslServerCredentialsOptions sslOpts(requestType);

    if(!gen::LoadFile(rootCerts, sslOpts.pem_root_certs, errMsg))
        return nullptr;

    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;

    for(const std::pair<std::string, std::string>& pair : keyCertPairs)
    {
        if(!gen::LoadFile(pair.first, pkcp.private_key, errMsg))
            return nullptr;

        if(!gen::LoadFile(pair.second, pkcp.cert_chain, errMsg))
            return nullptr;

        sslOpts.pem_key_cert_pairs.push_back(pkcp);
    }

    return grpc::SslServerCredentials(sslOpts);
}

// Build server credentials
inline std::shared_ptr<grpc::ServerCredentials> GetServerCredentials(
        const std::string& rootCert,
        const std::string& privateKey,
        const std::string& certChain,
        grpc_ssl_client_certificate_request_type requestType,
        std::string& errMsg)
{
    grpc::SslServerCredentialsOptions sslOpts(requestType);

    if(!LoadFile(rootCert, sslOpts.pem_root_certs, errMsg))
        return nullptr;

    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;

    if(!LoadFile(privateKey, pkcp.private_key, errMsg))
        return nullptr;

    if(!LoadFile(certChain, pkcp.cert_chain, errMsg))
        return nullptr;

    sslOpts.pem_key_cert_pairs.push_back(pkcp);

    return grpc::SslServerCredentials(sslOpts);
}

// Serialize protobuf::Message to json
inline bool ProtobufToJson(
        const ::google::protobuf::Message& msg,
        std::string& json,
        std::string& errMsg, bool compact=true)
{
    google::protobuf::util::JsonPrintOptions jsonOptions;
    //jsonOptions.always_print_primitive_fields = true; // deprecated
    jsonOptions.always_print_fields_with_no_presence = true;

    if(!compact)
        jsonOptions.add_whitespace = true;

    auto status = google::protobuf::util::MessageToJsonString(msg, &json, jsonOptions);
    if(!status.ok())
    {
        errMsg = status.ToString();
        return false;
    }

    return true;
}

// De-Serialize json to protobuf::Message
inline bool JsonToProtobuf(
        ::google::protobuf::Message& msg,
         const std::string& json,
         std::string& errMsg)
{
    auto status = google::protobuf::util::JsonStringToMessage(json, &msg);
    if(!status.ok())
    {
        errMsg = status.ToString();
        return false;
    }
    return true;
}

#ifndef __FNAME__
    // This constexpr method extracts the filename from a full path using
    // the __FILE__ preprocessor variable, resolving at compile time.
    constexpr const char* fname(const char* file, int i)
    {
        return (i == 0) ? (file) : (*(file + i) == '/' ? (file + i + 1) : fname(file, i - 1));
    }
    #define __FNAME__ gen::fname(__FILE__, sizeof(__FILE__)-1)
#endif

} //namespace gen

inline std::ostream& operator<<(std::ostream& out, const ::google::protobuf::Message& msg)
{
    google::protobuf::util::JsonPrintOptions jsonOptions;
    //jsonOptions.always_print_primitive_fields = true; // deprecated
    jsonOptions.always_print_fields_with_no_presence = true;
    std::string json;
    auto status = google::protobuf::util::MessageToJsonString(msg, &json, jsonOptions);
    out << (status.ok() ? json : status.ToString());
    return out;
}

#endif // __GRPC_UTILS_HPP__
// *INDENT-ON*


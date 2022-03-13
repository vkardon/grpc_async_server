// *INDENT-OFF*
//
// grpcUtils.hpp
//
#ifndef __GRPC_UTILS_HPP__
#define __GRPC_UTILS_HPP__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <grpcpp/grpcpp.h>
#pragma GCC diagnostic pop

namespace gen {

// Helper to format DNS address uri
inline std::string FormatDnsAddressUri(const char* host, unsigned short port)
{
    return "dns:" + std::string(host) + ":" + std::to_string(port);
}

// Helper to format Unix Domain Socket address uri
inline std::string FormatUnixDomainSocketAddressUri(const char* domainSocketPath)
{
    if(domainSocketPath[0] == '\0')
    {
        // NOTE: Supported since GRPC 1.37.0

        // For Unix domain socket in abstract namespace (Unix systems only)
        // unix-abstract:abstract_path
        //    abstract_path indicates a name in the abstract namespace.
        //    The name has no connection with filesystem pathnames.
        //    No permissions will apply to the socket - any process/user may access the socket.
        //    The underlying implementation of Abstract sockets uses a null byte ('\0') as the first character.
        //    The implementation will prepend this null. Do not include the null in abstract_path.
        //    abstract_path cannot contain null bytes.
        return "unix-abstract:" + std::string(domainSocketPath + 1);
    }
    else
    {
        // For Unix domain sockets (Unix systems only)
        // unix:path
        // unix://absolute_path
        //    path indicates the location of the desired socket.
        //    In the first form, the path may be relative or absolute;
        //    In the second form, the path must be absolute
        //    (i.e., there will actually be three slashes, two prior to the path and another to begin the absolute path).
        //
        return "unix://" + std::string(domainSocketPath);
    }
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

} //namespace gen

#endif // __GRPC_UTILS_HPP__
// *INDENT-ON*


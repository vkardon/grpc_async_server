// *INDENT-OFF*
//
// grpcUtils.hpp
//
#ifndef __GRPC_UTILS_HPP__
#define __GRPC_UTILS_HPP__

namespace gen {

// Helper to formst DNS address uri
inline std::string FormatDnsAddressUri(const char* hostname, unsigned short port)
{
    return "dns:" + std::string(hostname) + ":" + std::to_string(port);
}

// Helper to formst Unix Domain Socket address uri
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

} //namespace gen

#endif // __GRPC_UTILS_HPP__
// *INDENT-ON*


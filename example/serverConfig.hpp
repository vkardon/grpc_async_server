//
// serverConfig.hpp
//
#ifndef __SERVER_CONFIG_HPP__
#define __SERVER_CONFIG_HPP__

//
// gRpc server port number or socket name
//
#define PORT_NUMBER                  50055
#define UNIX_DOMAIN_SOCKET_PATH      "/tmp/grpc_server_test.sock"
#define UNIX_ABSTRACT_SOCKET_PATH    "grpc_server_test.sock"

//
// Thread-safe logging
//
#include <iostream>         // cout
#include <mutex>            // mutex, unique_lock
#include <unistd.h>         // syscall()
#include <sys/syscall.h>    // __NR_gettid

namespace logger
{
// Mutex to sync multi-threading logging
inline static std::mutex sLogMutex;

inline pid_t GetThreadId()
{
    static thread_local pid_t threadId = syscall(__NR_gettid);
    return threadId;
}
} // end of namespace logger

#define __MSG__(msg_type, msg)                                  \
do{                                                             \
    std::unique_lock<std::mutex> lock(logger::sLogMutex);       \
    std::cout << "[" << logger::GetThreadId() << "]"            \
              << (*msg_type == '\0' ? "" : "[" msg_type "]")    \
              << " " << __func__ << ": " << msg << std::endl;   \
}while(0)

#define OUTMSG(msg)    __MSG__("", msg)
#define INFOMSG(msg)   __MSG__("INFO", msg)
#define ERRORMSG(msg)  __MSG__("ERROR", msg)

//
// Helper StopWatch class to measure elapsed time
//
#include <string>   // std::string
#include <chrono>   // std:chrono
#include <iomanip>  // std::setw

class StopWatch
{
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::chrono::time_point<std::chrono::high_resolution_clock> stop;
    std::string prefix;

public:
    StopWatch(const char* _prefix="") : prefix(_prefix)
    {
        start = std::chrono::high_resolution_clock::now();
    }
    ~StopWatch()
    {
        stop = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = stop - start;
        std::cout << prefix << duration.count() << " sec" << std::endl;
    }
};

#endif // __SERVER_CONFIG_HPP__


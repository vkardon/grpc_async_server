#ifndef __SERVER_CONFIG_HPP__
#define __SERVER_CONFIG_HPP__

//
// gRpc server port number or socket name
//
#define PORT_NUMBER                         50055
#define UNIX_DOMAIN_SOCKET_PATH             "/tmp/grpc_server_test.sock"
#define UNIX_DOMAIN_ABSTRACT_SOCKET_PATH    "\0grpc_server_test.sock"

//
// Thread-safe logging
//
#include <iostream>         // cout
#include <mutex>            // mutex, unique_lock
#include <sys/syscall.h>    // syscall()

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
// Helper CTimeElapsed class to measure elapsed time
//
#include <string>	// std::string
#include <sys/time.h>   // gettimeofday()

class CTimeElapsed
{
    struct timeval start_tv;
    struct timeval stop_tv;
    std::string prefix;

public:
    CTimeElapsed(const char* _prefix="") : prefix(_prefix) { gettimeofday(&start_tv, NULL); }
    ~CTimeElapsed()
    {
        gettimeofday(&stop_tv, NULL);

        timeval tmp;
        if(stop_tv.tv_usec < start_tv.tv_usec)
        {
            tmp.tv_sec = stop_tv.tv_sec - start_tv.tv_sec - 1;
            tmp.tv_usec = 1000000 + stop_tv.tv_usec - start_tv.tv_usec;
        }
        else
        {
            tmp.tv_sec = stop_tv.tv_sec - start_tv.tv_sec;
            tmp.tv_usec = stop_tv.tv_usec - start_tv.tv_usec;
        }

        printf("%s%ld.%06lu sec\n", prefix.c_str(), tmp.tv_sec, (long)tmp.tv_usec);
    }
};

#endif // __SERVER_CONFIG_HPP__


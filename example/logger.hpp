#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

#include <iostream>         // cout
#include <mutex>            // mutex, unique_lock
#include <sys/syscall.h>    // syscall()

//
// Multi-threaded logger access methods
//
namespace logger
{
// Mutex to sync multi-threading logging
inline static std::mutex sLogMutex;

inline pid_t GetThreadId()
{
    static thread_local pid_t threadId = syscall(__NR_gettid);
    return threadId;
}
}

//
// Thread-safe logging
//
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

#endif // __LOGGER_HPP__


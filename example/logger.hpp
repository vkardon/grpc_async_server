#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

#include <iostream> // cout
#include <sstream>  // ostringstream
#include <mutex>    // mutex, unique_lock

#include <unistd.h>
#include <sys/syscall.h>

//
// Multi-threaded logger access methods
//
namespace logger
{
inline std::mutex& GetMutex()
{
    // Mutex to protect logger in multi-threading logging
    static std::mutex sLoggerMutex;
    return sLoggerMutex;
}

inline const std::string& SetThreadPrefix(const char* newPrefix)
{
    // Thread-local storage for thread-specific prefix string
    static thread_local std::string sLoggerThreadPrefix;
    if(newPrefix)
        sLoggerThreadPrefix = newPrefix;
    return sLoggerThreadPrefix;
}

inline const std::string& GetThreadPrefix()
{
    return SetThreadPrefix(nullptr);
}

inline pid_t GetThreadId()
{
    static thread_local pid_t threadId = syscall(__NR_gettid);
    return threadId;
}
}

//
// Thread-safe logging
//
#define MSG_MT(msg_type, msg)                                   \
do{                                                             \
    std::unique_lock<std::mutex> lock(logger::GetMutex());      \
    std::cout << "[" << logger::GetThreadId() << "]"            \
              << (*msg_type == '\0' ? "" : "[" msg_type "]")    \
              << logger::GetThreadPrefix() << " " << __func__   \
              << ": " << msg << std::endl;          \
}while(0)

#define OUTMSG_MT(msg)    MSG_MT("", msg)
#define INFOMSG_MT(msg)   MSG_MT("INFO", msg)
#define ERRORMSG_MT(msg)  MSG_MT("ERROR", msg)

#endif // __LOGGER_HPP__


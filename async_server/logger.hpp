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
namespace Logger
{
    inline std::mutex& GetMutex()
    {
        // Mutex to protect logger in multi-threading logging
        static std::mutex sLoggerMutex;
        return sLoggerMutex;
    }

    inline const char* SetThreadPrefix(const char* newPrefix)
    {
        // Thread-local storage for thread-specific prefix string
#ifdef __APPLE__
        static __thread     const char* sLoggerThreadPrefix = ""; 
#else
        static thread_local const char* sLoggerThreadPrefix = "";
#endif 
        if(newPrefix)
            sLoggerThreadPrefix = newPrefix;
        return sLoggerThreadPrefix;
    }

    inline const char* GetThreadPrefix() { return SetThreadPrefix(nullptr); }
}

#ifdef __APPLE__
    static uint64_t GetThreadId()
    {
        uint64_t tid;
        pthread_threadid_np(NULL, &tid);
        return tid;
    }
    static thread_local const pid_t threadId = GetThreadId();
#else
    static thread_local const pid_t threadId = syscall(__NR_gettid);
#endif

//
// Thread-safe logging
//
#define MSG_MT(msg_type, msg)                                   \
do{                                                             \
    std::unique_lock<std::mutex> lock(Logger::GetMutex());      \
    std::cout << (*msg_type == '\0' ? "" : "[" msg_type "] ")   \
              << "[" << threadId << "] " << __func__ << ": "    \
              << Logger::GetThreadPrefix() << msg << std::endl; \
}while(0)                                                       

#define OUTMSG_MT(msg)    MSG_MT("", msg)
#define INFOMSG_MT(msg)   MSG_MT("INFO", msg)
#define ERRORMSG_MT(msg)  MSG_MT("ERROR", msg)

#endif // __LOGGER_HPP__


#ifndef __TEST_SERVER_CONFIG_HPP__
#define __TEST_SERVER_CONFIG_HPP__

#define PORT_NUMBER                         50055
#define UNIX_DOMAIN_SOCKET_PATH             "/tmp/grpc_server_test.sock"
#define UNIX_DOMAIN_ABSTRACT_SOCKET_PATH    "\0grpc_server_test.sock"

#define URI "dns"
//#define URI "domain_socket"
//#define URI "domain_abstract_socket"

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
        //long long elapsed = (stop_tv.tv_sec - start_tv.tv_sec)*1000000 + (stop_tv.tv_usec - start_tv.tv_usec);
        //printf("%lld microseconds\n", elapsed);

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

#endif // __TEST_SERVER_CONFIG_HPP__

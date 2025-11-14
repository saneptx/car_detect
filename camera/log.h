#include <stdio.h>
#include <time.h>

// 输出时间戳
#define LOG_TIMESTAMP() \
    ({ \
        time_t now = time(NULL); \
        struct tm* time_info = localtime(&now); \
        printf("[%04d-%02d-%02d %02d:%02d:%02d] ", \
            time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday, \
            time_info->tm_hour, time_info->tm_min, time_info->tm_sec); \
    })

// LOG_DEBUG 宏定义，支持函数名、行号和参数
#define LOG_DEBUG(fmt, ...) \
    do { \
        LOG_TIMESTAMP(); \
        printf("DEBUG: [%s:%d] %s() - " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        LOG_TIMESTAMP(); \
        printf("INFO: [%s:%d] %s() - " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

#define LOG_WARNING(fmt, ...) \
    do { \
        LOG_TIMESTAMP(); \
        printf("WARNING: [%s:%d] %s() - " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

#define LOG_ERROR(fmt, ...) \
    do { \
        LOG_TIMESTAMP(); \
        printf("ERROR: [%s:%d] %s() - " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

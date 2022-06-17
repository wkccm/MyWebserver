#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    static Log* getInstance()
    {
        static Log instance;
        return &instance;
    }
    //异步写入
    static void* flushLogThread(void* args)
    {
        Log::getInstance()->asyncWriteLog();
    }

    bool init(const char *fileName, int closeLog, int logBufSize = 8192, int splitLines = 5000000, int maxQueueSize = 0);

    void writeLog(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    //设置虚析构
    virtual ~Log();
    //异步写入
    void *asyncWriteLog();

private:
    char dirName[128];  //路径名
    char logName[128];  //日志文件名
    int m_splitLines;   //日志最大行数
    int m_logBufSize;   //日志缓冲区大小
    long long m_count;  //日志行数
    int m_today;        //记录当天日期
    FILE *m_fp;         //日志文件指针
    char *m_buf;        //缓冲区指针
    BlockQueue<string> *m_logQueue; //阻塞队列
    bool m_isAsync;     //同步标志位
    locker m_mutex;     //互斥锁
    int m_closeLog;     //关闭日志
};

#define LOG_DEBUG(format, ...) if(m_closeLog == 0) {Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(m_closeLog == 0) {Log::getInstance()->writeLog(1, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(m_closeLog == 0) {Log::getInstance()->writeLog(2, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(m_closeLog == 0) {Log::getInstance()->writeLog(3, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#endif
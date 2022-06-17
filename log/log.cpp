#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>

using namespace std;

//默认状态下日志行数为0，同步写入
Log::Log()
{
    m_count = 0;
    m_isAsync = false;
}
//析构时关闭流
Log::~Log()
{
    if(m_fp != NULL){
        fclose(m_fp);
    }
}
//初始化日志
bool Log::init(const char* fileName, int closeLog, int logBufSize, int splitLines, int maxQueueSize)
{
    //当队列上限存在时，为异步日志，设置标志位，初始化日志阻塞队列，创建更新日志的线程
    if(maxQueueSize >= 1)
    {
        m_isAsync = true;
        m_logQueue = new BlockQueue<string>(maxQueueSize);
        pthread_t tid;
        pthread_create(&tid, NULL, flushLogThread, NULL);
    }

    //设置日志缓冲区
    m_closeLog = closeLog;
    m_logBufSize = logBufSize;
    m_buf = new char[m_logBufSize];
    memset(m_buf, '\0', m_logBufSize);
    m_splitLines = splitLines;

    //处理时间
    time_t t = time(NULL);
    struct tm *sysTm = localtime(&t);
    struct tm myTm = *sysTm;

    //处理文件路径
    const char *p = strrchr(fileName, '/');
    char logFullName[256] = {0};

    if(p == NULL)
    {
        snprintf(logFullName, 255, "%d_%02d_%02d_%s", myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday, logName);
    }
    else{
        strcpy(logName, p + 1);
        strncpy(dirName, fileName, p - fileName + 1);
        snprintf(logFullName, 255, "%s%d_%02d_%02d_%s", dirName, myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday, logName);
    }

    //新建文件
    m_today = myTm.tm_mday;
    m_fp = fopen(logFullName, "a");
    if(m_fp == NULL)
    {
        return false;
    }
    return true;
}

//写入日志
void Log::writeLog(int level, const char* format, ...)
{
    //处理时间
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sysTm = localtime(&t);
    struct tm myTm = *sysTm;

    //日志类型
    char s[16] = {0};
    switch(level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    //日志按天记录，处理溢出，注意刷新流
    m_mutex.lock();
    m_count++;

    if(m_today != myTm.tm_mday || m_count % m_splitLines ==0)
    {
        char newLog[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday);

        if(m_today != myTm.tm_mday)
        {
            snprintf(newLog, 255, "%s%s%s", dirName, tail, logName);
            m_today = myTm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(newLog, 255, "%s%s%s.%lld", dirName, tail, logName, m_count / m_splitLines);
        }

        m_fp = fopen(newLog, "a");
    }

    m_mutex.unlock();

    //用va_list处理不定参数，跟踪format
    va_list valist;
    va_start(valist, format);
    string logStr;

    m_mutex.lock();

    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday, myTm.tm_hour, myTm.tm_min, myTm.tm_sec, now.tv_usec, s);

    int m = vsnprintf(m_buf + n, m_logBufSize - 1, format, valist);

    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    logStr = m_buf;

    m_mutex.unlock();

    va_end(valist);

    // 异步日志，队列中添加一个元素
    // 同步日志，直接打印日志
    if(m_isAsync && !m_logQueue->full())
    {
        m_logQueue->push(logStr);
    }
    else
    {
        m_mutex.lock();
        fputs(logStr.c_str(), m_fp);
        m_mutex.unlock();
    }

}

//异步写入，处理队列，处理写入
void* Log::asyncWriteLog()
{
    string singleLog;
    while (m_logQueue->pop(singleLog))
    {
        m_mutex.lock();
        fputs(singleLog.c_str(), m_fp);
        m_mutex.unlock();
    }
}

//刷新流
void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}


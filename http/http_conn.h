#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/timer.h"
#include "../log/log.h"

class HttpConn
{
public:
    // 文件名长度最大值
    static const int FILENAME_LEN = 200;
    // 读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区最大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // http请求方法
    enum method
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 解析状态
    enum checkState
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    // hhtp响应状态码
    enum httpCode
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    // 行的状态
    enum lineStatus
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    HttpConn(){}
    ~HttpConn(){}

    // 初始化连接
    void init(int socketfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭连接
    void closeConn(bool realClose = true);
    void process();
    bool readOnce();
    bool write();
    sockaddr_in *getAddress()
    {
        return &m_address;
    }
    void initMysqlResult(ConnectionPool *connPool);
    int timerFlag;
    int improv;

private:
    void init();
    httpCode processRead();
    bool processWrite(httpCode ret);
    httpCode parseRequestLine(char *text);
    httpCode parseHeaders(char *text);
    httpCode parseContent(char *text);
    httpCode doRequest();
    char *getLine()
    {
        return m_readBuf + m_startLine;
    }
    lineStatus parseLine();
    void unmap();
    bool addResponse(const char *format, ...);
    bool addContent(const char *content);
    bool addStatusLine(int status, const char *title);
    bool addHeaders(int contentLength);
    bool addContentType();
    bool addContentLength(int contentLength);
    bool addLinger();
    bool addBlankLine();

public:
    // epoll的第一个参数
    static int m_epollfd;
    // 当前用户数
    static int m_userCount;
    // mysql结构体
    MYSQL *mysql;
    // 线程池状态
    int m_state;

private:
    // 套接字描述符
    int m_sockfd;
    // 套接字地址
    sockaddr_in m_address;
    // 读缓冲
    char m_readBuf[READ_BUFFER_SIZE];
    // 读指针位置
    int m_readIdx;
    // 当前检测指针位置
    int m_checkIdx;
    // 记录起始位置
    int m_startLine;
    // 写缓冲区
    char m_writeBuf[WRITE_BUFFER_SIZE];
    // 写指针位置
    int m_writeIdx;
    // 当前的解析状态
    checkState m_checkState;
    // http请求类型
    method m_method;
    // 文件真实地址
    char m_realFile[FILENAME_LEN];
    // 请求的连接url
    char *m_url;
    // http版本
    char *m_version;
    // 发送请求的主机ip
    char *m_host;
    // 内容长度
    int m_contentLength;
    // 是否是长连接
    bool m_linger;
    // 文件在内存中的地址
    char *m_fileAddress;
    // 描述文件属性的结构
    struct stat m_fileStat;
    // 分散/集中IO辅助数组
    struct iovec m_iv[2];
    // 数组的个数
    int m_ivCount;
    // 协助区分GET和POST
    int cgi;
    // 辅助处理SQL请求
    char *m_string;
    // 还需发送的数据
    int bytesToSend;
    // 已经发送的数据
    int bytesHaveSend;
    // 文件根目录
    char *docRoot;
    // 触发模式
    int m_trigMode;
    // 是否关闭日志
    int m_closeLog;
    // SQL辅助数组
    char sqlUser[100];
    char sqlPasswd[100];
    char sqlName[100];
};

#endif
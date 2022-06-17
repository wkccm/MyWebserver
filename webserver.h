#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

// 连接文件描述符上限
const int MAX_FD = 65536;
// 最大事件数
const int MAX_EVENT_NUMBER = 10000;
// 时间槽大小
const int TIMESLOT = 5;

class Webserver
{
public:
    Webserver();
    ~Webserver();

    void init(int port, string user, string password, string dataBaseName, int logWrite, int optLinger, int trigMode, int sqlNum, int threadNum, int closeLog, int actorModel);

    void threadPool();
    void sqlPool();
    void logWrite();
    void trigMode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in clientAddress);
    void adjustTimer(UtilTimer *timer);
    void dealTimer(UtilTimer *timer, int sockfd);
    bool dealClientData();
    bool dealWithSignal(bool &timeout, bool &stopServer);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);

public:
    // 端口号
    int m_port;
    // root路径
    char *m_root;
    // 日志是否同步
    int m_logWrite;
    // 是否关闭日志
    int m_closeLog;
    // 并发模式
    int m_actorModel;
    // 管道描述符
    int m_pipefd[2];
    // epoll描述符
    int m_epollfd;
    // http连接指针
    HttpConn *users;
    // 数据库连接池指针
    ConnectionPool *m_connPool;
    // 用户名
    string m_user;
    // 密码
    string m_password;
    // 数据库名
    string m_dataBaseName;
    // 数据库最大连接数量
    int m_sqlNum;
    // 线程池指针
    ThreadPool<HttpConn> *m_pool;
    // 最大线程数
    int m_threadNum;
    // epoll结构体数组
    epoll_event events[MAX_EVENT_NUMBER];
    // 服务器总连接描述符
    int m_listenfd;
    // 优雅关闭连接
    int m_optLinger;
    // 触发模式
    int m_trigMode;
    // 总连接描述符触发方式
    int m_listenTrigMode;
    // http连接描述符触发方式
    int m_connTrigMode;
    // 用户信息级及定时器
    ClientData *usersTimer;
    // 工具类
    Utils utils;
};

#endif
#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include <time.h>
#include "../log/log.h"

// 前向声明
class UtilTimer;

// 客户端发送的数据包括：客户端地址、套接字描述符、
struct ClientData
{
    sockaddr_in address;
    int sockfd;
    UtilTimer *timer;
};

// 定时器
class UtilTimer
{
public:
    // 链表前驱、后继
    UtilTimer():prev(NULL), next(NULL) {}

public:
    // 超时时间
    time_t expire;
    // 连接超时时触发
    void cb_func(ClientData *);
    // 代表一个用户请求，包含用户信息
    ClientData *userData;
    // 前驱
    UtilTimer *prev;
    // 后继
    UtilTimer *next;
};

// 定时器升序链表
class SortTimerList
{
public:
    SortTimerList();
    ~SortTimerList();

    // 增加一个事件
    void addTimer(UtilTimer *timer);
    // 重新排序链表，当由于某些事件导致链表升序被破坏时触发
    void adjustTimer(UtilTimer *timer);
    // 删除一个事件
    void delTimer(UtilTimer *timer);
    // 定时滴答，证明运转正常
    void tick();

private:
    // 增加一个事件，为使函数逻辑清晰，使用辅助函数
    void addTimer(UtilTimer *timer, UtilTimer *listHead);
    // 链表头部
    UtilTimer *head;
    // 链表尾部
    UtilTimer *tail;
};

// 定时器工具类
class Utils
{
public:
    Utils(){}
    ~Utils(){}

    // 初始化，设置时间槽
    void init(int timeslot);
    // 将文件描述符设置为非阻塞状态
    int setnonblocking(int fd);
    // 向内核事件表注册一个读事件，指定epolloneshot模式，指定触发模式
    void addfd(int epollfd, int fd, bool oneShot, int trigMode);
    // 信号处理函数
    static void sigHandler(int sig);
    // 设置信号函数
    void addSig(int sig, void(handler)(int), bool restart = true);
    // 定时处理任务，不断触发SIGALRM信号
    void timerHandler();
    // 报错函数
    void showError(int connfd, const char *info);

public:
    // 管道描述符
    static int *uPipefd;
    // 定时器链表
    SortTimerList m_TimerList;
    // epoll描述符
    static int uEpollfd;
    // 时间槽大小
    int m_timeslot;
};

#endif
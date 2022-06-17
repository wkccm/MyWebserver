#include "time.h"
#include "../http/http_conn.h"

// 构造
SortTimerList::SortTimerList()
{
    head = NULL;
    tail = NULL;
}

// 析构时依次释放节点
SortTimerList::~SortTimerList()
{
    UtilTimer *tmp = head;
    while(tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

//增加一个节点，在本函数中处理参数的有效性和原链表的头部是否存在，如不存在设置成参数节点，若应该放到头结点则直接放置，不必调用辅助函数
void SortTimerList::addTimer(UtilTimer *timer)
{
    if(!timer)
        return;
    if(!head)
    {
        head = tail = timer;
        return;
    }
    if(timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    addTimer(timer, head);
}

// 在链表可能失序时，维持链表的有序性，只向后移动节点
void SortTimerList::adjustTimer(UtilTimer *timer)
{
    if(!timer)
        return;
    UtilTimer *tmp = timer->next;
    if(!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        addTimer(timer, head);
    }
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        addTimer(timer, timer->next);
    }
}

// 删除一个节点
void SortTimerList::delTimer(UtilTimer *timer)
{
    if(!timer)
    {
        return;
    }
    // 只有一个节点
    if((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 头部
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 尾部
    if(timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 解指针
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
    return;
}

// 
void SortTimerList::tick()
{
    if(!head)
    {
        return;
    }
    // 当前时间
    time_t cur = time(NULL);
    UtilTimer *tmp = head;
    while(tmp)
    {
        // 未超时，则无事发生
        if(cur < tmp->expire)
        {
            break;
        }
        // 超时，调用处理函数
        tmp->cb_func(tmp->userData);
        // 更换链表头部
        head = tmp->next;
        if(head)
        {
            head->prev = NULL;
        }
        // 删除超时节点
        delete tmp;
        // 再处理下一个节点
        tmp = head;
    }
}

// 增加节点的辅助函数，调用前已经保证了参数的有效性
void SortTimerList::addTimer(UtilTimer *timer, UtilTimer *listHead)
{
    UtilTimer *prev = listHead;
    UtilTimer *tmp = prev->next;
    while(tmp)
    {
        if(timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // 在链表尾部插入
    if(!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

// 初始化设置时间槽大小
void Utils::init(int timeslot)
{
    m_timeslot = timeslot;
}

// 将文件描述符设置为非阻塞
int Utils::setnonblocking(int fd)
{
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return newOption;
}


// 内核事件表注册事件
void Utils::addfd(int epollfd, int fd, bool oneShot, int trigMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(trigMode == 1)
    {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else
    {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(oneShot)
    {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 需要保证函数的可重入性，保存原来的errno
void Utils::sigHandler(int sig)
{
    int saveErrno = errno;
    int msg = sig;
    send(uPipefd[1], (char *)&msg, 1, 0);
    errno = saveErrno;
    return;
}

// 设置信号函数
void Utils::addSig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
    return;
}

void Utils::timerHandler()
{
    m_TimerList.tick();
    alarm(m_timeslot);
    return;
}

void Utils::showError(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
    return;
}

// 为cb_func函数提供初始值
int *Utils::uPipefd = 0;
int Utils::uEpollfd = 0;

// 执行时代表定时器超时，需要清理失效socket
void UtilTimer::cb_func(ClientData *userData)
{
    // 删除超时连接在socket上的注册事件
    epoll_ctl(Utils::uEpollfd, EPOLL_CTL_DEL, userData->sockfd, 0);
    // 防止close错误
    assert(userData);
    // 关闭文件描述符，节省资源
    close(userData->sockfd);
    // 连接数减1
    HttpConn::m_userCount--;
}
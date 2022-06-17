#include "./webserver.h"

// 设置root路径、初始化用户数据
Webserver::Webserver()
{
    users = new HttpConn[MAX_FD];

    char serverPath[200];
    getcwd(serverPath, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(serverPath) + strlen(root) + 1);
    strcpy(m_root, serverPath);
    strcat(m_root, root);

    usersTimer = new ClientData[MAX_FD];
}

// 关闭各种描述符和管道，释放申请的空间
Webserver::~Webserver()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] usersTimer;
    delete m_pool;
}

// 设置初始值
void Webserver::init(int port, string user, string password, string dataBaseName, int logWrite, int optLinger, int trigMode, int sqlNum, int threadNum, int closeLog, int actorModel)
{
    m_port = port;
    m_user = user;
    m_password = password;
    m_dataBaseName = dataBaseName;
    m_sqlNum = sqlNum;
    m_threadNum = threadNum;
    m_logWrite = logWrite;
    m_optLinger = optLinger;
    m_trigMode = trigMode;
    m_closeLog = closeLog;
    m_actorModel = actorModel;
}

// 解析触发方式
void Webserver::trigMode()
{
    if(m_trigMode == 0)
    {
        m_listenTrigMode = 0;
        m_connTrigMode = 0;
    }
    else if(m_trigMode == 1)
    {
        m_listenTrigMode = 0;
        m_connTrigMode = 1;
    }
    else if(m_trigMode == 2)
    {
        m_listenTrigMode = 1;
        m_connTrigMode = 0;
    }
    else if(m_trigMode == 3)
    {
        m_listenTrigMode = 1;
        m_connTrigMode = 1;
    }
}

// 日志写入，分为同步和异步
void Webserver::logWrite()
{
    if(m_closeLog == 0)
    {
        if(m_logWrite == 1)
        {
            Log::getInstance()->init("./ServerLog", m_closeLog, 2000, 800000, 800);
        }
        else
        {
            Log::getInstance()->init("./ServerLog", m_closeLog, 2000, 800000, 0);
        }
    }
}

// 数据库连接池，创建单例
void Webserver::sqlPool()
{
    m_connPool = ConnectionPool::getInstance();
    m_connPool->init("localhost", m_user, m_password, m_dataBaseName, 3306, m_sqlNum, m_closeLog);
    users->initMysqlResult(m_connPool);
}

// 初始化线程池
void Webserver::threadPool()
{
    m_pool = new ThreadPool<HttpConn>(m_actorModel, m_connPool, m_threadNum);
}

// 事件监听函数，网络编程，设置服务器基本状态
void Webserver::eventListen()
{
    // IPV4、TCP
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);
    // 默认方式关闭TCP连接
    if(m_optLinger == 0)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    // 优雅关闭TCP连接
    else if(m_optLinger == 1)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    // 设置套接字地址结构体
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);
    // 设置选项，可重用socket
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    // 绑定
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    // 监听
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);
    // 初始化工具
    utils.init(TIMESLOT);
    // epoll事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    // 创建一个epoll句柄，size参数在现在的Linux版本中无意义
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
    // 将epoll与TCP连接绑定，设置参数
    utils.addfd(m_epollfd, m_listenfd, false, m_listenTrigMode);
    HttpConn::m_epollfd = m_epollfd;
    // 进程间通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addSig(SIGPIPE, SIG_IGN);
    utils.addSig(SIGALRM, utils.sigHandler, false);
    utils.addSig(SIGTERM, utils.sigHandler, false);

    alarm(TIMESLOT);

    Utils::uPipefd = m_pipefd;
    Utils::uEpollfd = m_epollfd;
}

void Webserver::timer(int connfd, struct sockaddr_in clientAddress)
{
    // http初始化
    users[connfd].init(connfd, clientAddress, m_root, m_connTrigMode, m_closeLog, m_user, m_password, m_dataBaseName);
    // 设置客户端信息、套接字描述符
    usersTimer[connfd].address = clientAddress;
    usersTimer[connfd].sockfd = connfd;
    // 定时器节点
    UtilTimer *timer = new UtilTimer;
    // 将设置的客户端信息存入定时器
    timer->userData = &usersTimer[connfd];
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    usersTimer[connfd].timer = timer;
    utils.m_TimerList.addTimer(timer);
}

// 接受一个定时器参数，设置该定时器的超时时间
void Webserver::adjustTimer(UtilTimer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    // 定时器链表更新
    utils.m_TimerList.adjustTimer(timer);

    LOG_INFO("%s", "adjust timer once");
}

// 超时时对定时器的处理
void Webserver::dealTimer(UtilTimer *timer, int sockfd)
{
    // 处理函数
    timer->cb_func(&usersTimer[sockfd]);
    // 移除处理后的计时器
    if(timer)
    {
        utils.m_TimerList.delTimer(timer);
    }
    // 打印日志
    LOG_INFO("close fd %d", usersTimer[sockfd].sockfd);
}

// 接受客户端的连接
bool Webserver::dealClientData()
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLength = sizeof(clientAddress);
    // listenfd触发为LT
    if(m_listenTrigMode == 0)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&clientAddress, &clientAddrLength);
        // 接受连接失败
        if(connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        // 用户数溢出，超过描述符
        if(HttpConn::m_userCount >= MAX_FD)
        {
            utils.showError(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 正常时对连接定时
        timer(connfd, clientAddress);
    }
    // listenfd触发为ET
    else
    {
        while(1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&clientAddress, &clientAddrLength);
            if(connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(HttpConn::m_userCount >= MAX_FD)
            {
                utils.showError(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, clientAddress);
        }
        return false;
    }
    return true;
}

// 处理信号，SIGALRM和SIGTERM出现时，服务器停止运行
bool Webserver::dealWithSignal(bool &timeout, bool &stopServer)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret == -1)
    {
        return false;
    }
    else if(ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stopServer = true;
                break;
            }
            }
        }
    }
    return true;
}

// 处理读，接受一个套接字描述符
void Webserver::dealWithRead(int sockfd)
{
    // 设置定时器
    UtilTimer *timer = usersTimer[sockfd].timer;
    // reactor模式
    if(m_actorModel == 1)
    {
        if(timer)
        {
            adjustTimer(timer);
        }

        m_pool->append(users + sockfd, 0);

        while(true)
        {
            if(users[sockfd].improv == 1)
            {
                // 若定时器标志位为1，则代表超时
                if(users[sockfd].timerFlag == 1)
                {
                    dealTimer(timer, sockfd);
                    users[sockfd].timerFlag = 0;
                }
                users[sockfd].improv = 0;
                // 避免重复处理，跳出循环
                break;
            }
        }
    }
    // proactor模式
    else
    {
        if(users[sockfd].readOnce())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].getAddress()->sin_addr));

            m_pool->append(users + sockfd, 0);
            if(timer)
            {
                adjustTimer(timer);
            }
        }
        else
        {
            dealTimer(timer, sockfd);
        }
    }
}

// 处理写
void Webserver::dealWithWrite(int sockfd)
{
    UtilTimer *timer = usersTimer[sockfd].timer;

    if(m_actorModel == 1)
    {
        if(timer)
        {
            adjustTimer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while(true)
        {
            if(users[sockfd].improv == 1)
            {
                if(users[sockfd].timerFlag == 1)
                {
                    dealTimer(timer, sockfd);
                    users[sockfd].timerFlag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        if(users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].getAddress()->sin_addr));

            // m_pool->append(users + sockfd ,1);
            if(timer)
            {
                adjustTimer(timer);
            }
        }
        else
        {
            dealTimer(timer, sockfd);
        }
    }
}

// 事件循环
void Webserver::eventLoop()
{
    bool timeout = false;
    bool stopServer = false;
    // SIGTERM出现时服务器停止运行
    while(!stopServer)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // epoll失败
        if(number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        // 循环处理
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            // 当描述符为读客户端数据时
            if(sockfd == m_listenfd)
            {
                dealClientData();
            }
            // 服务器关闭连接，移除定时器
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                UtilTimer *timer = usersTimer[sockfd].timer;
                dealTimer(timer, sockfd);
            }
            // 若处理信号失败，则在日志中打印错误
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealWithSignal(timeout, stopServer);
                if(flag == false)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理读
            else if(events[i].events & EPOLLIN)
            {
                dealWithRead(sockfd);
            }
            // 处理写
            else if(events[i].events & EPOLLOUT)
            {
                dealWithWrite(sockfd);
            }
        }
        // SIGALRM时，滴答一声
        if(timeout)
        {
            utils.timerHandler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}
#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>

const char *ok_200_title = "ok";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void HttpConn::initMysqlResult(ConnectionPool *connPool)
{
    MYSQL *mysql = NULL;
    ConnectionRAII mysqlConn(&mysql, connPool);
    // 查询成功返回0，不成功返回非0
    if(mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        // mysql_error返回上一次查询的错误信息
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // mysql_store_result返回上一次mysql_query的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中列数
    int numFields = mysql_num_fields(result);

    // 将结果集以结构数组的形式表示，此处多余
    // MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 将用户名和密码存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 辅助函数，将文件描述符设置为非阻塞，标志位的按位并操作
int setNonBlocking(int fd)
{
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return newOption;
}

// 注册读事件
void addfd(int epollfd, int fd, bool oneShot, int trigMode)
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
    setNonBlocking(fd);
}

// 删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 重置事件
void modfd(int epollfd, int fd, int ev, int trigMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(trigMode == 1)
    {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }
    else
    {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConn::m_userCount = 0;
int HttpConn::m_epollfd = -1;

// 关闭一个连接
void HttpConn::closeConn(bool realClose)
{
    if(realClose && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_userCount--;
    }
}

// 初始化一个连接，重置参数
void HttpConn::init(int sockfd, const sockaddr_in &addr, char *root, int trigMode, int closeLog, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_trigMode);
    m_userCount++;

    docRoot = root;
    m_trigMode = trigMode;
    m_closeLog = closeLog;

    strcpy(sqlUser, user.c_str());
    strcpy(sqlPasswd, passwd.c_str());
    strcpy(sqlName, sqlname.c_str());

    init();
}

// 参数初始化，分配缓冲区
void HttpConn::init()
{
    mysql = NULL;
    bytesToSend = 0;
    bytesHaveSend = 0;
    m_checkState = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_contentLength = 0;
    m_host = 0;
    m_startLine = 0;
    m_checkIdx = 0;
    m_readIdx = 0;
    m_writeIdx = 0;
    cgi = 0;
    m_state = 0;
    timerFlag = 0;
    improv = 0;

    memset(m_readBuf, '\0', READ_BUFFER_SIZE);
    memset(m_writeBuf, '\0', WRITE_BUFFER_SIZE);
    memset(m_realFile, '\0', FILENAME_LEN);
}

// 以下为状态机
// 分析语法，返回当前行的状态
HttpConn::lineStatus HttpConn::parseLine()
{
    char temp;
    // m_checkIdx为检测指针，m_readIdx为buffer尾部的下一个字节
    // http语法以\r\n为结尾
    for (; m_checkIdx < m_readIdx; ++m_checkIdx)
    {
        temp = m_readBuf[m_checkIdx];
        if(temp == '\r')
        {
            if((m_checkIdx + 1) == m_readIdx)
            {
                return LINE_OPEN;
            }
            else if(m_readBuf[m_checkIdx + 1] == '\n')
            {
                m_readBuf[m_checkIdx++] = '\0';
                m_readBuf[m_checkIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n')
        {
            if(m_checkIdx > 1 && m_readBuf[m_checkIdx - 1] == '\r')
            {
                m_readBuf[m_checkIdx - 1] = '\0';
                m_readBuf[m_checkIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 读缓冲增加，指针向后移动
bool HttpConn::readOnce()
{
    if(m_readIdx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytesRead = 0;
    // LT读数据
    if(m_trigMode == 0)
    {
        bytesRead = recv(m_sockfd, m_readBuf + m_readIdx, READ_BUFFER_SIZE - m_readIdx, 0);
        
        if(bytesRead <= 0)
        {
            return false;
        }
        m_readIdx += bytesRead;
        return true;
    }
    // ET读数据
    else
    {
        while(true)
        {
            bytesRead = recv(m_sockfd, m_readBuf + m_readIdx, READ_BUFFER_SIZE - m_readIdx, 0);
            if(bytesRead == -1)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                return false;
            }
            else if(bytesRead == 0)
            {
                return false;
            }
            m_readIdx += bytesRead;
        }
        return true;
    }
}

// 获得请求方法，目标url和版本号
HttpConn::httpCode HttpConn::parseRequestLine(char *text)
{
    m_url = strpbrk(text, " \t");
    if(!m_url)
    {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';
    char *method = text;
    if(strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");

    if(!m_version)
    {
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if(strcasecmp(m_version,"HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url,"http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    if(strlen(m_url) == 1)
    {
        strcat(m_url, "judge.html");
    }

    m_checkState = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的头部
HttpConn::httpCode HttpConn::parseHeaders(char *text)
{
    if(text[0] == '\0')
    {
        if(m_contentLength != 0)
        {
            m_checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_contentLength = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 检验http请求是否完全读入
HttpConn::httpCode HttpConn::parseContent(char *text)
{
    if(m_readIdx >= (m_contentLength + m_checkIdx))
    {
        text[m_contentLength] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 状态转移
HttpConn::httpCode HttpConn::processRead()
{
    lineStatus lineStatus = LINE_OK;
    httpCode ret = NO_REQUEST;
    char *text = 0;

    while((m_checkState == CHECK_STATE_CONTENT && lineStatus == LINE_OK) || ((lineStatus = parseLine()) == LINE_OK))
    {
        text = getLine();
        m_startLine = m_checkIdx;
        LOG_INFO("%s", text);
        switch(m_checkState)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parseRequestLine(text);
            if(ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parseHeaders(text);
            if(ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
                
            else if(ret == GET_REQUEST)
                return doRequest();
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parseContent((text));
            if(ret == GET_REQUEST)
                return doRequest();
            lineStatus = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 处理http请求
HttpConn::httpCode HttpConn::doRequest()
{
    strcpy(m_realFile, docRoot);
    int len = strlen(docRoot);
    const char *p = strrchr(m_url, '/');

    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char flag = m_url[1];
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/");
        strcat(m_urlReal, m_url + 2);
        strncpy(m_realFile + len, m_urlReal, FILENAME_LEN - len - 1);
        free(m_urlReal);

        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
        {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
        {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        if(*(p + 1) == '3')
        {
            char *sqlInsert = (char *)malloc(sizeof(char) * 200);
            strcpy(sqlInsert, "INSERT INTO user(username,passwd) VALUES('");
            strcat(sqlInsert, name);
            strcat(sqlInsert, "', '");
            strcat(sqlInsert, password);
            strcat(sqlInsert, "')");

            if(users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sqlInsert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if(!res)
                {
                    strcpy(m_url, "/log.html");
                }
                else
                {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else
            {
                strcpy(m_url, "registerError.html");
            }
        }
        else if(*(p + 1) == '2')
        {

            if(users.find(name) != users.end() && users[name] == password)
            {
                strcpy(m_url, "/welcome.html");
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if(*(p + 1) == '0')
    {
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/register.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    }
    else if(*(p + 1) == '1')
    {
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/log.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    }
    else if(*(p + 1) == '5')
    {
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/picture1.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    }
    else if(*(p + 1) == '6')
    {
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/picture2.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    }
    else if(*(p + 1) == '7')
    {
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/video.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    }
    else
    {
        strncpy(m_realFile + len, m_url, FILENAME_LEN - len - 1);
    }

    if(stat(m_realFile, &m_fileStat) < 0)
    {

        return NO_RESOURCE;
    }
    // 检查是否可读
    if(!(m_fileStat.st_mode & S_IROTH))
    {

        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_fileStat.st_mode))
    {
        return BAD_REQUEST;
    }
    // 用到了上面解析得到的真实文件地址
    int fd = open(m_realFile, O_RDONLY);
    // 进程私有，可读，不指定内存位置
    m_fileAddress = (char *)mmap(0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // 映射后及时关闭描述符，节约资源
    close(fd);
    return FILE_REQUEST;
}

// 释放内存空间，完成后将文件地址清除
void HttpConn::unmap()
{
    if(m_fileAddress)
    {
        munmap(m_fileAddress, m_fileStat.st_size);
        m_fileAddress = 0;
    }
}

// 处理写入
bool HttpConn::write()
{
    int temp = 0;
    // 当无数据可发送时，重置为读事件
    if(bytesToSend == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigMode);
        init();
        return true;
    }

    while(1)
    {
        temp = writev(m_sockfd, m_iv, m_ivCount);


        if(temp < 0)
        {
            // 当缓冲区满时，重置为写事件
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trigMode);
                return true;
            }
            unmap();
            return false;
        }

        bytesHaveSend += temp;
        bytesToSend -= temp;

        // 处理大文件，每次写时更新指针，否则会导致断传
        if(bytesHaveSend >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_fileAddress + (bytesHaveSend - m_writeIdx);
            m_iv[1].iov_len = bytesToSend;
        }
        else
        {
            m_iv[0].iov_base = m_writeBuf + bytesHaveSend;
            m_iv[0].iov_len = m_iv[0].iov_len - bytesHaveSend;
        }

        if(bytesToSend <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigMode);

            if(m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

// 向写缓冲区中继续写入，需判断溢出问题，用va_list辅助处理可变参数
bool HttpConn::addResponse(const char *format, ...)
{
    // 初始时指针已不合法
    if(m_writeIdx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list argList;
    va_start(argList, format);
    int len = vsnprintf(m_writeBuf + m_writeIdx, WRITE_BUFFER_SIZE - 1 - m_writeIdx, format, argList);
    // 一次写溢出
    if(len >= WRITE_BUFFER_SIZE-1-m_writeIdx)
    {
        va_end(argList);
        return false;
    }
    m_writeIdx += len;
    va_end(argList);

    LOG_INFO("request: %s", m_writeBuf);
    return true;
}

// 写入时，日志记录当前状态
bool HttpConn::addStatusLine(int status, const char *title)
{
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 处理头部时将其转化为三个子函数，此处可扩展cookie
bool HttpConn::addHeaders(int contentLen)
{
    return addContentLength(contentLen) && addLinger() && addBlankLine();
}

bool HttpConn::addContentLength(int contentLen)
{
    return addResponse("Content-Length: %d\r\n", contentLen);
}

bool HttpConn::addLinger()
{
    return addResponse("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::addBlankLine()
{
    return addResponse("%s", "\r\n");
}

// 添加返回信息
bool HttpConn::addContent(const char *content)
{
    return addResponse("%s", content);
}

// 根据返回状态码做出相应处理
bool HttpConn::processWrite(httpCode ret)
{
    switch(ret)
    {
    case INTERNAL_ERROR:
    {
        addStatusLine(500, error_500_title);
        addHeaders(strlen(error_500_form));
        if(!addContent(error_500_form))
        {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        addStatusLine(404, error_404_title);
        addHeaders(strlen(error_404_form));
        if(!addContent(error_404_form))
        {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        addStatusLine(403, error_403_title);
        addHeaders(strlen(error_403_form));
        if(!addContent(error_403_form))
        {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        addStatusLine(200, ok_200_title);
        if(m_fileStat.st_size != 0)
        {
            addHeaders(m_fileStat.st_size);
            m_iv[0].iov_base = m_writeBuf;
            m_iv[0].iov_len = m_writeIdx;
            m_iv[1].iov_base = m_fileAddress;
            m_iv[1].iov_len = m_fileStat.st_size;
            m_ivCount = 2;
            bytesToSend = m_writeIdx + m_fileStat.st_size;
            return true;
        }
        else
        {
            const char *okString = "<html><body></body></html>";
            addHeaders(strlen(okString));
            if(!addContent(okString))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_writeBuf;
    m_iv[0].iov_len = m_writeIdx;
    m_ivCount = 1;
    bytesToSend = m_writeIdx;
    return true;
}

// 处理epoll
void HttpConn::process()
{
    httpCode readRet = processRead();
    if(readRet == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigMode);
        return;
    }
    bool writeRet = processWrite(readRet);
    if(!writeRet)
    {
        closeConn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trigMode);
}
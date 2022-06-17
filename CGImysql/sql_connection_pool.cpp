#include <mysql/mysql.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

// 初始化时将现有连接数、空闲连接数置零
ConnectionPool::ConnectionPool()
{
    m_curConn = 0;
    m_freeConn = 0;
}

// 析构时销毁连接池
ConnectionPool::~ConnectionPool()
{
    destroyPool();
}

//单例模式
ConnectionPool *ConnectionPool::getInstance()
{
    static ConnectionPool connPool;
    return &connPool;
}

// 初始化连接池
void ConnectionPool::init(string url, string user, string password, string dataBaseName, int port, int maxConn, int closeLog)
{
    m_url = url;
    m_port = port;
    m_user = user;
    m_password = password;
    m_dataBaseName = dataBaseName;
    m_closeLog = closeLog;

    // 逐个初始化
    for (int i = 0; i < maxConn; i++)
    {
        MYSQL *conn = NULL;
        conn = mysql_init(conn);

        if(conn == NULL)
        {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }

        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), dataBaseName.c_str(), port, NULL, 0);

        if(conn == NULL)
        {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }

        connList.push_back(conn);
        ++m_freeConn;
    }

    reserve = sem(m_freeConn);
    m_maxConn = m_freeConn;
}

// 获取一个连接
MYSQL *ConnectionPool::getConnection()
{
    MYSQL *conn = NULL;

    if(connList.size() == 0)
    {
        return NULL;
    }

    reserve.wait();

    lock.lock();

    conn = connList.front();
    connList.pop_front();

    --m_freeConn;
    ++m_curConn;

    lock.unlock();
    return conn;
}

// 释放一个连接
bool ConnectionPool::releaseConnection(MYSQL *conn)
{
    if(conn == NULL)
    {
        return false;
    }

    lock.lock();

    connList.push_back(conn);
    ++m_freeConn;
    --m_curConn;

    lock.unlock();

    reserve.post();
    return true;
}

// 销毁连接池
void ConnectionPool::destroyPool()
{
    lock.lock();

    if(connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end();++it)
        {
            MYSQL *conn = *it;
            mysql_close(conn);
        }
        m_curConn = 0;
        m_freeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

// 获取当前空闲连接数
int ConnectionPool::getFreeConn()
{
    return this->m_freeConn;
}

// 封装连接池
// 获取一个连接
ConnectionRAII::ConnectionRAII(MYSQL **SQL, ConnectionPool *connPool)
{
    *SQL = connPool->getConnection();
    connRAII = *SQL;
    poolRAII = connPool;
}

// 释放一个连接
ConnectionRAII::~ConnectionRAII()
{
    poolRAII->releaseConnection(connRAII);
}
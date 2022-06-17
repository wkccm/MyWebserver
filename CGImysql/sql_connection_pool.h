#ifndef SQL_CONNECTION_POOL
#define SQL_CONNECTION_POOL

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <string>
#include <iostream>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class ConnectionPool
{
public:
    MYSQL *getConnection();
    bool releaseConnection(MYSQL *conn);
    int getFreeConn();
    void destroyPool();

    static ConnectionPool *getInstance();

    void init(string url, string user, string password, string dataBaseName, int port, int maxConn, int closeLog);

private:
    ConnectionPool();
    ~ConnectionPool();

    int m_maxConn;
    int m_curConn;
    int m_freeConn;
    locker lock;
    list<MYSQL *> connList;
    sem reserve;

public:
    string m_url;
    string m_port;
    string m_user;
    string m_password;
    string m_dataBaseName;
    int m_closeLog;
};

class ConnectionRAII
{
public:
    ConnectionRAII(MYSQL **con, ConnectionPool *connPool);
    ~ConnectionRAII();

private:
    MYSQL *connRAII;
    ConnectionPool *poolRAII;
};

#endif
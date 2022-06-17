/*
    创建Config类，配置服务器参数
*/


#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

class Config
{
private:
    //
public:
    Config();
    ~Config(){};

    //命令语法分支解析
    void parse_arg(int argc, char *argv[]);

    //端口号
    int port;

    //日志写入方式
    int logWrite;

    //触发组合模式
    int trigMode;

    //listenfd触发模式
    int listenTrigMode;

    //connfd触发模式
    int connTrigMode;

    //优雅关闭链接
    int optLinger;

    //数据库连接池数量
    int sqlNum;

    //线程池线程数量
    int threadNum;

    //是否关闭日志
    int closeLog;

    //并发模式选择
    int actorModel;
};

#endif
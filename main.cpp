#include "config.h"

int main(int argc, char *argv[])
{
    // 此处需要使用者自己设置，连接数据库
    string user = "root";
    string passwd = "12345678";
    string databasename = "yourdb";

    // 读取命令行的参数进行设置
    Config config;
    config.parse_arg(argc, argv);

    // 服务器
    Webserver server;
    // 初始化
    server.init(config.port, user, passwd, databasename, config.logWrite, config.optLinger, config.trigMode, config.sqlNum, config.threadNum, config.closeLog, config.actorModel);
    // 开启日志
    server.logWrite();
    // 开启数据库连接池
    server.sqlPool();
    // 开启线程池
    server.threadPool();
    // 解析触发方式
    server.trigMode();
    // 设置服务器状态
    server.eventListen();
    // 启动服务器，循环处理请求
    server.eventLoop();

    return 0;
}
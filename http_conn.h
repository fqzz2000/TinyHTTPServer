#ifndef HTTPCONN_H
#define HTTPCONN_H
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
class http_conn
{
public:
    static int m_epollfd;    // 所有socket事件按都被注册到同一个epollfd中
    static int m_user_count; // 统计用户数量
    http_conn() {}
    ~http_conn() {}

    void process();                                 // 处理客户端请求
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接受的连接
    void close_conn();                              // 关闭连接
    bool read();                                    // 非阻塞读
    bool write();                                   // 非阻塞写

private:
    int m_sockfd;          // 该http连接的socket
    sockaddr_in m_address; // 通信的socket地址
};

#endif
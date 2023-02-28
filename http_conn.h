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
#include <string.h>
#include "locker.h"
// HTTP 请求方法
enum METHOD
{
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT
};
// 主状态机可能状态

enum CHECK_STATE
{
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};
// 从状态机的可能状态
// 读取到完整的行
// 行出错
// 行数据尚不完整
enum LINE_STATUS
{
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN
};

// 报文解析结果
enum HTTP_CODE
{
    NO_REQUEST,        // 请求不完整
    GET_REQUEST,       // 获得了完整客户请求
    BAD_REQUEST,       // 请求语法错误
    NO_RESOURCE,       // 服务器没有资源
    FORBIDDEN_REQUEST, // 无访问权限
    FILE_REQUEST,      // 文件请求，获取成功
    INTERNAL_ERROR,    // 内部错误
    CLOSED_CONNECTION  // 表示客户端已经关闭了连接

};

class http_conn
{
public:
    static int m_epollfd;                      // 所有socket事件按都被注册到同一个epollfd中
    static int m_user_count;                   // 统计用户数量
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小
    http_conn() {}
    ~http_conn() {}

    void process();                                 // 处理客户端请求
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接受的连接
    void close_conn();                              // 关闭连接
    bool read();                                    // 非阻塞读
    bool write();                                   // 非阻塞写

private:
    int m_sockfd;                      // 该http连接的socket
    sockaddr_in m_address;             // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    int m_read_idx;                    // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置

    int m_chekced_idx; // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;  // 当前正在解析的行的起始位置
    char *m_url;       // 请求目标文件名
    char *m_version;   // 协议版本，只支持HTTP1.1
    METHOD m_method;   // 请求方法

    char *m_host;  // 主机名
    bool m_linger; // http是否保持连接

    CHECK_STATE m_check_state; // 主状态机当前所处状态

    void init(); // 初始化连接其余的信息

    HTTP_CODE process_read();                 // 解析HTTP请求
    HTTP_CODE parse_request_line(char *text); // 解析请求首行
    HTTP_CODE parse_headers(char *text);      // 解析请求头
    HTTP_CODE parse_content(char *text);      // 解析请求体

    LINE_STATUS parse_line(); // 获取一行
    char *get_line() { return &m_read_buf[m_start_line]; }
    HTTP_CODE do_request();
};

#endif
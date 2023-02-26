#include "http_conn.h"

int http_conn::m_epollfd = -1;   // 所有socket事件按都被注册到同一个epollfd中
int http_conn::m_user_count = 0; // 统计用户数量

// 设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 添加文件描述符到epoll时间表中
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; // 水平出发和异常断开 TODO： 可以通过配置文件更改
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll事件表中删除文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符, 重置socket的EPOLLONEHOT事件，确保下次可读EPOLLIN可以被触发
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true); // set oneshot event
    m_user_count++;
}
// 关闭连接
void http_conn::close_conn()
{
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd); // remove
        m_sockfd = -1;
        m_user_count--; // 关闭连接客户总数量 -1
    }
}

bool http_conn::read()
{
    printf("一次性读完数据");
    return true;
} // 非阻塞读
bool http_conn::write()
{
    printf("一次性写完数据");
    return true;
} // 非阻塞写

void http_conn::process()
{
    // 解析HTTP请求

    printf("parse request, create response");
    // 生成响应
}
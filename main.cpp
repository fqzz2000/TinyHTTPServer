#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535          // 最大文件描述符个数
#define MAX_EVENT_NUMBER 1000 // 监听最大事件数量

// 添加信号捕捉
void addsig(int sig, void(handler)(int))
{
    // 注册信号的参数
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符到epoll时间表中
extern void addfd(int epollfd, int fd, bool one_shot);
// 从epoll事件表中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("usage: %s port_number\n", basename(argv[0]));
        exit(-1); // shabi
    }

    // 获取端口号
    int port = atoi(argv[1]);
    printf("hi %d\n", port);
    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 初始化线程池
    threadpool<http_conn> *pool = NULL; // 最好处理和保存信息分开 for http_conn
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (...)
    {
        exit(EXIT_FAILURE);
    }

    http_conn *users = new http_conn[MAX_FD];

    // 网络代码
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    // TODO: 判断bind error
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));

    // 监听
    listen(listenfd, 5);

    // 创建epoll对象，事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 监听的文件描述符添加到epoll列表中
    addfd(epollfd, listenfd, false); // 监听描述符不需oneshot
    http_conn::m_epollfd = epollfd;

    // 主线程轮询事件发生
    while (true)
    {

        // 检测到的事件数量
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        printf("num %d\n", num);
        if ((num < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < num; i++)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                // 有客户端连接了
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);
                printf("connfd is %d", connfd);
                if (http_conn::m_user_count >= MAX_FD)
                {
                    // 目前连接数满了
                    // TODO: 给客户端写一个信息，服务器内部正忙
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化放到数组中
                users[connfd].init(connfd, client_address); // 连接文件描述符默认是递增的
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 对方异常断开或者错误等事件
                // 关闭连接
                printf("connection end\n");
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN)
            {
                // 如果可读事件
                if (users[sockfd].read())
                {
                    // 一次性把所有数据读完
                    pool->append(&users[sockfd]);
                }
                else
                {
                    // 如果读失败了，关闭连接
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // 如果可写
                if (!users[sockfd].write())
                {
                    users[sockfd].close_conn();
                } // 一次写完所有数据，否则关闭连接
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    return EXIT_SUCCESS;
}
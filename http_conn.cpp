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
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // 水平出发和异常断开 TODO： 可以通过配置文件更改
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
    m_user_count++;                   // 总用户数 + 1

    init();
}
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求首行
    m_chekced_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;

    // 清空读缓冲
    bzero(m_read_buf, READ_BUFFER_SIZE);
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
// 循环读取数据知道无数据可读或者对方关闭连接
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    // 读取到的字节
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, &m_read_buf[m_read_idx], READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 没有数据了
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("读取到了数据: %s\n", m_read_buf);
    return true;
} // 非阻塞读

HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || (line_status = parse_line()) == LINE_OK)
    {
        // 解析到了一行完整的数据，或者解析到了请求体，也是完整的数据

        // 获取一行数据
        text = get_line();

        m_start_line = m_chekced_idx;
        printf("got 1 http line: %s\n", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                return do_request(); // 解析具体请求信息
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN; // 数据不完整
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
        }
        }
    }
    return NO_REQUEST;
} // 解析HTTP请求
// 解析HTTP请求行，获取请求方法，目标URL， HTTP版本
HTTP_CODE http_conn::parse_request_line(char *text)
{
    // GET /index.html HTTP/1.1
    // 可以用正则表达式
    // 这里用传统方式
    m_url = strpbrk(text, " \t");
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    // http://192.168.1.1:10000/index.html
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;                 // 192.168.1.1:10000/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; // 主状态机状态转移到检查请求头
    return NO_REQUEST;
} // 解析请求首行
HTTP_CODE http_conn::parse_headers(char *text)
{
    return NO_REQUEST;
} // 解析请求头
HTTP_CODE http_conn::parse_content(char *text)
{
    return NO_REQUEST;
} // 解析请求体

// 解析一行，判断依据\r\n
LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_chekced_idx < m_read_idx; ++m_chekced_idx)
    {
        temp = m_read_buf[m_chekced_idx];
        if (temp == '\r')
        {
            // 读到头了没有\n
            if ((m_chekced_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_chekced_idx + 1] == '\n')
            {
                m_read_buf[m_chekced_idx++] = '\n';
                m_read_buf[m_chekced_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            // WHY NOT >= 1？？？？？
            if ((m_chekced_idx > 1) && (m_read_buf[m_chekced_idx - 1] == '\r'))
            {
                m_read_buf[m_chekced_idx - 1] = '\0';
                m_read_buf[m_chekced_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
} // 获取一行

HTTP_CODE http_conn::do_request()
{
    return NO_REQUEST;
}

bool http_conn::write()
{
    printf("一次性写完数据");
    return true;
} // 非阻塞写

void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // process_read()  ;
    printf("parse request, create response");
    // 生成响应
}
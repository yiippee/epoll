//start from the very beginning,and to create greatness
//@author: Chuangwei Lin
//@E-mail：979951191@qq.com
//@brief： 一个epoll的简单例子,服务端
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/epoll.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <vector>
#include <algorithm>
#include <iostream>
//epoll_event结构体如下
//typedef union epoll_data{
//  void* ptr;
//  int fd;
//  uint32_t u32;
//  uint64_t u64;
//}epoll_data_t;
//struct epoll_event{
//  uint32 events;
//  epoll_data_t data;
//}
typedef std::vector<struct epoll_event> EventList;
//错误输出宏
#define ERR_EXIT(m) \
    do \
    { \
        perror(m); \
        exit(EXIT_FAILURE); \
    } while(0)


ssize_t socket_write(int sockfd, const char* buffer, size_t buflen)
{
    ssize_t tmp;
    size_t total = buflen;
    const char* p = buffer;

    while(1)
    {
        tmp = write(sockfd, p, total);
        if(tmp < 0)
        {
            // 当send收到信号时,可以继续写,但这里返回-1.
            if(errno == EINTR) return -1;
            // 当socket是非阻塞时,如返回此错误,表示写缓冲队列已满,
            // 在这里做延时后再重试.
            if(errno == EAGAIN)
            {
                usleep(1000);
                continue;
            }
            return -1;
        }
        if((size_t)tmp == total) return buflen;

         total -= tmp;
         p += tmp;
    }

    return tmp;//返回已写字节数
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    //为解决EMFILE事件，先创建一个空的套接字
    int idlefd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    int listenfd;
    //if ((listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    //创建一个socket套接字
    if ((listenfd = socket(PF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP)) < 0)
        ERR_EXIT("socket");
    //填充IP和端口
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(5188);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int on = 1;
    //设置地址的重新利用
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        ERR_EXIT("setsockopt");
    }
    //绑定地址和端口
    if (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    //监听
    if (listen(listenfd, SOMAXCONN) < 0) {
        ERR_EXIT("listen");
    }
    std::vector<int> clients;
    int epollfd;
    //创建epollfd，epoll_create1函数可以指定一个选项
    epollfd = epoll_create1(EPOLL_CLOEXEC);

    struct epoll_event event;
    event.data.fd = listenfd;
    //默认触发模式是LT模式（电平出发模式），或上EPOLLET变成ET模式（边沿触发）
    event.events = EPOLLIN;
    //把listenfd事件添加到epollfd进行管理
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);
    ///定义事件列表，初始状态为16个，不够时进行倍增
    EventList events(16);
    struct sockaddr_in peeraddr;
    socklen_t peerlen;
    int connfd;
    int nready;
    while (1) {
        //epoll_wait返回的时间都是活跃的，
        // events为输出参数，返回时events都是活跃的，储存所有的读写事件
        //nready为返回的事件个数
        nready = epoll_wait(epollfd, &*events.begin(), static_cast<int>(events.size()), -1);
        if (nready == -1) {
            if (errno == EINTR) continue;// 被信号中断
            ERR_EXIT("epoll_wait");
        }
        if (nready == 0) continue;//没有事件发生

        //如果事件的数量达到预定义的上限值
        if ((size_t)nready == events.size()) {
            events.resize(events.size()*2);//扩充为原来的两倍
        }
        // 依次处理就绪了的event
        for (int i = 0; i < nready; ++i) {
            if (events[i].data.fd == listenfd) { // 有新的连接
                //如果监听套接字处于活跃的状态
                peerlen = sizeof(peeraddr);
                //accept这个连接
                connfd = ::accept4(listenfd, (struct sockaddr*)&peeraddr,&peerlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (connfd == -1) {
                    if (errno == EMFILE) {
                        //EMFILE错误处理，接受然后优雅地断开
                        close(idlefd);
                        idlefd = accept(listenfd, NULL, NULL);
                        close(idlefd);
                        idlefd = open("/dev/null", O_RDONLY | O_CLOEXEC);
                        continue;
                    } else {
                        ERR_EXIT("accept4");
                    }
                }
                //打印IP和端口信息
                std::cout<<"ip="<<inet_ntoa(peeraddr.sin_addr)<<" port="<<ntohs(peeraddr.sin_port)<<std::endl;

                clients.push_back(connfd);
                event.data.fd = connfd;
                event.events = EPOLLIN;//或EPOLLET变成ET模式
                //把新接受的事件加入关注
                epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &event);
            } else if (events[i].events & EPOLLIN) {//如果是pollin事件,可读。
                //取出文件描述符
                connfd = events[i].data.fd;
                if (connfd < 0) continue;
                //读取内容
                while(1) {
                    //缓冲区
                    char buf[4] = {0};
                    int ret = read(connfd, buf, 3);
                    // std::cout << "ret: " << ret << std::endl;
                    if (ret == -1) {//出错
                        //ERR_EXIT("read");
                        std::cout << "read done." << std::endl;
                        break;
                    }
                    if (ret == 0) {
                        //返回0表示对方关闭了
                        std::cout<<"client close"<<std::endl;
                        close(connfd);
                        event = events[i];
                        //把套接字剔除出去
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, connfd, &event);
                        clients.erase(std::remove(clients.begin(), clients.end(), connfd), clients.end());
                        break;
                    }
                    // 打印到控制台
                    //printf("recv: %s \n", buf);
                    //将消息发送回去
                    std::cout << "recv: " << buf << std::endl;
                    fflush(stdout);
                    //write(connfd, buf, strlen(buf));
                }

                // 完全读完了数据
                std::cout << "we have read from the client : ";
                //设置用于写操作的文件描述符
                event.data.fd = connfd;
                //设置用于注册的写操作事件
                event.events |= EPOLLOUT;
                //修改sockfd上要处理的事件为EPOLLOUT
                epoll_ctl(epollfd, EPOLL_CTL_MOD, connfd, &event);
            } else if (events[i].events & EPOLLOUT) {
                //有数据待发送，写socket
                char* buf = "write...\n";
                //int n = write(connfd, buf, strlen(buf));
                int n = socket_write(connfd, buf, strlen(buf));
                //设置用于写操作的文件描述符
                event.data.fd = connfd;
                //设置用于注册的写操作事件
                event.events &= ~EPOLLOUT;
                //event.events = EPOLLOUT;
                //修改sockfd上要处理的事件为EPOLIN
                epoll_ctl(epollfd, EPOLL_CTL_MOD, connfd, &event);
            }
        }
    }

    return 0;
}

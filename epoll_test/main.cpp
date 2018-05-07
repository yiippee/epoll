#include <unistd.h>
#include <iostream>
#include <sys/epoll.h>
using namespace std;
int main(void)
{
    int epfd, nfds;
    struct epoll_event ev, events[5];//ev用于注册事件，数组用于返回要处理的事件
    epfd = epoll_create(1);//只需要监听一个描述符——标准输出
    ev.data.fd = STDOUT_FILENO;
    ev.events = EPOLLOUT | EPOLLET;//监听读状态同时设置ET模式
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDOUT_FILENO, &ev);//注册epoll事件
    for(;;)
    {
        nfds = epoll_wait(epfd, events, 5, -1);
        for(int i = 0;i < nfds; i++)
        {
            if(events[i].data.fd == STDOUT_FILENO)
                cout << "hello world!";

            ev.data.fd=STDOUT_FILENO;
            ev.events=EPOLLOUT|EPOLLET;
            epoll_ctl(epfd,EPOLL_CTL_MOD,STDOUT_FILENO,&ev); //重新MOD事件（ADD无效）
        }
   }
}
/*
要解决上述两个ET模式下的读写问题，我们必须实现：
    a. 对于读，只要buffer中还有数据就一直读；
    b. 对于写，只要buffer还有空间且用户请求写的数据还未写完，就一直写。
*/

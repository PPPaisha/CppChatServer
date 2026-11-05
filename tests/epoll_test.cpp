//------------------------------------------------------
// 文件名:epoll_test.cpp
// 创建日期:2026-11-5
// 创建者:pppaisha
// 文件描述:用于测试epoll事件驱动模型的功能
//------------------------------------------------------
#include "net/epoll.h"

#include <unistd.h>
#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
{
    int fds[2];
    if(::pipe(fds) == -1)
    {
        throw std::runtime_error("pipe failed");
    }

    Epoll epoll;
    epoll.add(fds[0], EPOLLIN);

    //管道当前没有数据
    int count = epoll.wait(0);
    assert(count == 0);

    //向管道写入一个字节
    char message = 'A';
    ssize_t n = ::write(fds[1], &message, 1);
    assert(n == 1);

    //读端现在应该已经就绪
    count = epoll.wait(1000);
    assert(count == 1);

    const epoll_event& event = epoll.eventAt(0);
    assert(event.data.fd == fds[0]);
    assert(event.events & EPOLLIN);

    //消费管道中的数据
    char buffer = 0;
    n = ::read(fds[0], &buffer, 1);
    assert(n == 1);
    assert(buffer == 'A');

    //数据被读完后 不应该继续报告可读
    count = epoll.wait(0);
    assert(count == 0);

    epoll.remove(fds[0]);

    ::close(fds[0]);
    ::close(fds[1]);

    std::cout<<"Epoll test passed!"<<std::endl;
    return 0;
}
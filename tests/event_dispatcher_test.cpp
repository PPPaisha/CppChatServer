//------------------------------------------------------
// 文件名:event_dispatcher_test.cpp
// 创建日期:2026-11-5
// 创建者:pppaisha
// 文件描述:事件分发器测试文件
//------------------------------------------------------
#include "net/event_dispatcher.h"

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

    EventDispatcher dispatcher;
    int callbackCount = 0;
    char received = 0;
    dispatcher.add(fds[0],EPOLLIN,[&](uint32_t events)
    {
        assert(events & EPOLLIN);
        ssize_t n = ::read(fds[0],&received,1);
        assert(n == 1);
        ++callbackCount;
    })

    //没有数据 不应该执行回调
    int count = dispatcher.dispatch(0);
    assert(count == 0);
    assert(callbackCount == 0);

    //写入数据 触发EPOLLIN事件
    char message = 'B';
    ssize_t n = ::write(fds[1],&message,1);
    assert(n == 1);

    count = dispatcher.dispatch(1000);
    assert(count == 1);
    assert(callbackCount == 1);
    assert(received == 'B');

    //数据已经读完 不应该重复触发
    count = dispatcher.dispatch(0);
    assert(count == 0);
    assert(callbackCount == 1);

    dispatcher.remove(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
    std::cout<<"EventDispatcher test passed!"<<std::endl;
    return 0;

}
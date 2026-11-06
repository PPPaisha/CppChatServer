//------------------------------------------------------
// 文件名:event_loop_test.cpp
// 创建日期:2026-11-6
// 创建者:pppaisha
// 文件描述:事件循环处理测试文件
//------------------------------------------------------
#include "net/event_loop.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>
#include <stdexcept>

int main()
{
    EventLoop loop;
    std::exception_ptr threadError;

    std::thread worker([&]
    {
        try
        {
            loop.run();
        }
        catch(...)
        {
            threadError = std::current_exception();
        }
    });

    //等待事件循环真正启动
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(!loop.isRunning() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if(!loop.isRunning())
    {
        worker.join();
        throw std::runtime_error("EventLoop failed to stop");
    }

    //让EventLoop有机会进入epoll_wait()
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    //等待EventLoop线程退出
    worker.join();
    if(threadError)
    {
        std::rethrow_exception(threadError);
    }

    if(loop.isRunning())
    {
        throw std::runtime_error("EventLoop did not stop");
    }

    std::cout<<"EventLoop wakeup test passed!"<<std::endl;
    return 0;
}
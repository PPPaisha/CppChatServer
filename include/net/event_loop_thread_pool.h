//------------------------------------------------------
// 文件名:event_loop_thread_pool.h
// 创建日期:2026-11-20
// 创建者:pppaisha
// 文件描述:线程池管理EventLoop线程的头文件
//------------------------------------------------------
#pragma once

#include "net/event_loop_thread.h"

#include <cstddef>
#include <memory>
#include <vector>

class EventLoop;
class EventLoopThreadPool
{
public:
    explicit EventLoopThreadPool(EventLoop& baseLoop);
    void start(std::size_t threadCount);
    EventLoop* getNextLoop();
    void stop();

private:
    EventLoop& m_baseLoop;
    std::vector<std::unique_ptr<EventLoopThread>> m_threads;
    std::vector<EventLoop*> m_loops;

    std::size_t m_next = 0;
    bool m_started = false;
};

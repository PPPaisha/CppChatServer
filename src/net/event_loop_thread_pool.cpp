//------------------------------------------------------
// 文件名:event_loop_thread_pool.cpp
// 创建日期:2026-11-20
// 创建者:pppaisha
// 文件描述:线程池管理EventLoop线程的实现文件
//------------------------------------------------------
#include "net/event_loop_thread_pool.h"
#include "net/event_loop.h"

#include <stdexcept>

EventLoopThreadPool::EventLoopThreadPool(EventLoop& baseLoop)
    :m_baseLoop(baseLoop)
{    
}

void EventLoopThreadPool::start(std::size_t threadCount)
{
    if(m_started)
    {
        throw std::logic_error("EventLoopThreadPool already started");
    }
    m_started = true;
    try
    {
        for(std::size_t i=0; i<threadCount; ++i)
        {
            auto thread = std::make_unique<EventLoopThread>();
            EventLoop* loop = thread->startLoop();
            m_threads.push_back(std::move(thread));
            m_loops.push_back(loop);
        }
    }
    catch(...)
    {
        stop();
        throw;
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    if(!m_started)
    {
        throw std::logic_error("EventLoopThreadPool not started");
    }
    if(m_loops.empty())
    {
        return &m_baseLoop;
    }
    EventLoop* loop = m_loops[m_next];
    m_next = (m_next + 1) % m_loops.size();
    return loop;
}

void EventLoopThreadPool::stop()
{
    for(auto& thread:m_threads)
    {
        thread->stop();
    }
    m_loops.clear();
    m_threads.clear();
    m_next = 0;
    m_started = false;
}
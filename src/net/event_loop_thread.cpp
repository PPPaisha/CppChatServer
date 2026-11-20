//------------------------------------------------------
// 文件名:event_loop_thread.cpp
// 创建日期:2026-11-20
// 创建者:pppaisha
// 文件描述:负责启动线程 等待EventLoop初始化完成 并在析构时停止事件循环 回收线程
//------------------------------------------------------
#include "net/event_loop_thread.h"
#include "net/event_loop.h"

#include <stdexcept>

EventLoopThread::~EventLoopThread()
{
    stop();
}

EventLoop* EventLoopThread::startLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if(m_started)
    {
        throw std::logic_error("EventLoopThread already started");
    }
    m_started = true;
    m_thread = std::thread(&EventLoopThread::threadFunc,this);
    m_cv.wait(lock,[this]()
    {
        return m_ready;
    });
    if(m_error)
    {
        auto error = m_error;
        lock.unlock();
        m_thread.join();
        std::rethrow_exception(error);
    }
    return m_loop;
}

void EventLoopThread::threadFunc()
{
    try
    {
        EventLoop loop;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_loop = &loop;
            m_ready = true;
        }
        m_cv.notify_one();
        loop.run();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_loop = nullptr;
        }
    }
    catch(...)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = std::current_exception();
            m_loop = nullptr;
            m_ready = true;
        }
        m_cv.notify_one();
    }
}

void EventLoopThread::stop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if(!m_started)
    {return;}
    m_started = false;
    if(m_loop)
    {
        EventLoop* loop = m_loop;
        //将停止操作放入目标EventLoop的任务队列
        loop->queueInLoop([loop]()
        {
            loop->stop();
        });
    }
    lock.unlock();
    if(m_thread.joinable())
    {
        m_thread.join();
    }
    lock.lock();
    m_loop = nullptr;
}
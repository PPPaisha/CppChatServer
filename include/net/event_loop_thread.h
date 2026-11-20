//------------------------------------------------------
// 文件名:event_loop_thread.h
// 创建日期:2026-11-20
// 创建者:pppaisha
// 文件描述:事件循环线程头文件
//------------------------------------------------------
#pragma once

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

class EventLoop;

class EventLoopThread
{
public:
    EventLoopThread() = default;
    ~EventLoopThread();
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;
    EventLoop* startLoop();
    void stop();

private:
    void threadFunc();
    
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    EventLoop* m_loop = nullptr;
    std::exception_ptr m_error;
    bool m_started = false;
    bool m_ready = false;
};
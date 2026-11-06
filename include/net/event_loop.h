//------------------------------------------------------
// 文件名:event_loop.h
// 创建日期:2026-11-6
// 创建者:pppaisha
// 文件描述:事件循环处理头文件
//------------------------------------------------------
#pragma once

#include "net/event_dispatcher.h"

#include <atomic>

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    void run();
    void stop() noexcept;

    bool isRunning() const noexcept;

private:
    void handleWakeup(uint32_t events);

    EventDispatcher m_dispatcher;
    int m_wakeupFd = -1;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_started{false};
};
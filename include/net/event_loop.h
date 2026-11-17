//------------------------------------------------------
// 文件名:event_loop.h
// 创建日期:2026-11-6
// 创建者:pppaisha
// 文件描述:事件循环处理头文件
//------------------------------------------------------
#pragma once

#include "net/event_dispatcher.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

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
    bool isInLoopThread() const noexcept;

    using EventCallback = EventDispatcher::EventCallback;
    using Task = std::function<void()>;

    void add(int fd, uint32_t events, EventCallback callback);
    void modify(int fd, uint32_t events);
    void remove(int fd);
    void queueInLoop(Task task);
    void runInLoop(Task task);


private:
    void handleWakeup(uint32_t events);
    void executePendingTasks();

    EventDispatcher m_dispatcher;
    int m_wakeupFd = -1;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_started{false};
    std::mutex m_taskMutex;
    std::vector<Task> m_pendingTasks;
    std::thread::id m_loopThreadId;
};
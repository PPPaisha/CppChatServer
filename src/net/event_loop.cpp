//------------------------------------------------------
// 文件名:event_loop.cpp
// 创建日期:2026-11-6
// 创建者:pppaisha
// 文件描述:事件循环处理实现文件
//------------------------------------------------------
#include "net/event_loop.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <system_error>

EventLoop::EventLoop()
{
    //创建并注册唤醒fd
    m_wakeupFd = ::eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);
    if(m_wakeupFd == -1)
    {
        throw std::system_error(errno,std::system_category(),"Failed to create eventfd");
    }
    
    try
    {
        m_dispatcher.add(m_wakeupFd,EPOLLIN,[this](uint32_t events)
    {
        handleWakeup(events);
    });
    }
    catch(...)
    {
        ::close(m_wakeupFd);
        m_wakeupFd = -1;
        throw;
    }
}

EventLoop::~EventLoop()
{
    if(m_wakeupFd >= 0)
    {
        ::close(m_wakeupFd);
    }
}

void EventLoop::run()
{
    //尝试给m_started设置为true 如果m_started本身就是true 那么就抛出异常
    if(m_started.exchange(true))
    {
        throw std::runtime_error("EventLoop already started");
    }
    m_running.store(true);
    try
    {
        while(m_running.load())
        {
            m_dispatcher.dispatch(-1);
        }
    }
    catch(...)
    {
        m_running.store(false);
        throw;
    }
    m_running.store(false);
}

void EventLoop::stop() noexcept
{
    if(!m_running.exchange(false))
    {
        return;
    }
    uint64_t value = 1;
    ssize_t n;
    do
    {
        n = ::write(m_wakeupFd,&value,sizeof(value));
    }
    while(n == -1 && errno == EINTR);

}

bool EventLoop::isRunning() const noexcept
{
    return m_running.load();
}

void EventLoop::handleWakeup(uint32_t events)
{
    if(!events & EPOLLIN)
    {
        return;
    }
    uint64_t value = 0;
    while(true)
    {
        ssize_t n = ::read(m_wakeupFd,&value,sizeof(value));
        if(n == sizeof(value))
        {
            continue;
        }
        if(n == -1 && errno == EINTR)
        {
            continue;
        }
        if(n == -1 && errno == EAGAIN)
        {
            break;
        }

        throw std::system_error(n == -1 ? errno :EIO, std::generic_category(),"eventfd read failed");
    }
}

void EventLoop::add(int fd, uint32_t events, EventCallback callback)
{
    m_dispatcher.add(fd,events,std::move(callback));
}

void EventLoop::modify(int fd, uint32_t events)
{
    m_dispatcher.modify(fd,events);
}

void EventLoop::remove(int fd)
{
    m_dispatcher.remove(fd);
}
//------------------------------------------------------
// 文件名:event_dispatcher.cpp
// 创建日期:2026-11-5
// 创建者:pppaisha
// 文件描述:事件分发器实现
//------------------------------------------------------
#include "net/event_dispatcher.h"

#include <stdexcept>
#include <utility>

EventDispatcher::EventDispatcher (int maxEvents)
    :m_epoll(maxEvents)
{
}

void EventDispatcher::add(int fd, uint32_t events, EventCallback callback)
{
    if(!callback)
    {
        throw std::invalid_argument("Empty event callback");
    }

    if(m_callbacks.find(fd) != m_callbacks.end())
    {
        throw std::invalid_argument("FD already registered");
    }

    m_callbacks.emplace(fd,std::move(callback));
    try
    {
        m_epoll.add(fd,events);
    }
    catch(...)
    {
        m_callbacks.erase(fd);
        throw;
    }
}


void EventDispatcher::modify(int fd, uint32_t events)
{
    if(m_callbacks.find(fd) == m_callbacks.end())
    {
        throw std::invalid_argument("FD not registered");
    }
    m_epoll.modify(fd,events);
}

void EventDispatcher::remove(int fd)
{
    auto it = m_callbacks.find(fd);
    if(it == m_callbacks.end())
    {
        throw std::invalid_argument("FD not registered");
    }
    m_epoll.remove(fd);
    m_callbacks.erase(it);
}

int EventDispatcher::dispatch(int timeoutMs)
{
    int count = m_epoll.wait(timeoutMs);
    for(int i=0; i<count; ++i)
    {
        const auto& event = m_epoll.eventAt(i);
        auto it = m_callbacks.find(event.data.fd);
        if(it != m_callbacks.end())
        {
            auto callback = it->second;
            callback(event.events);
        }
    }
    return count;
}
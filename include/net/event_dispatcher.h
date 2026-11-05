//------------------------------------------------------
// 文件名:event_dispatcher.h
// 创建日期:2026-11-5
// 创建者:pppaisha
// 文件描述:事件分发器头文件
//------------------------------------------------------
#pragma once

#include "net/epoll.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

class EventDispatcher
{
public:
    using EventCallback = std::function<void(uint32_t)>;

    explicit EventDispatcher(int maxEvents = 64);

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    void add(int fd, uint32_t events, EventCallback callback);
    void modify(int fd, uint32_t events);
    void remove(int fd);

    int dispatch(int timeoutMs);

private:
    Epoll m_epoll;
    std::unordered_map<int, EventCallback> m_callbacks;

};
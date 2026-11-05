//------------------------------------------------------
// 文件名:epoll.cpp
// 创建日期:2026-11-5
// 创建者:pppaisha
// 文件描述:epoll事件驱动模型实现文件
//------------------------------------------------------
#include "net/epoll.h"

#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <system_error>

Epoll::Epoll(int maxEvents)
{
    if(maxEvents <= 0)
    {
        throw std::invalid_argument("maxEvents must be positive");
    }
    m_events.resize(maxEvents);

    epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if(epfd == -1)
    {
        throw std::system_error(errno, std::generic_category(), "epoll_create1 failed");
    }
}

Epoll::~Epoll()
{
    if(epfd >= 0)
    {
        ::close(epfd);
    }
}

void Epoll::add(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if(::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        throw std::system_error(errno, std::generic_category(),"epoll add failed");
    }
}

void Epoll::modify(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if(::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        throw std::system_error(errno, std::generic_category(), "epoll modify failed");
    }
}

void Epoll::remove(int fd)
{
    if(::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) == -1)
    {
        throw std::system_error(errno, std::generic_category(), "epoll remove failed");
    }
}

int Epoll::wait(int timeoutMs)
{
    int count;
    do
    {
        count = ::epoll_wait(epfd, m_events.data(), static_cast<int>(m_events.size()), timeoutMs);
    }while(count == -1 && errno == EINTR);
    if(count == -1)
    {
        throw std::system_error(errno, std::generic_category(), "epoll wait failed");
    }
    return count;
}

const epoll_event& Epoll::eventAt(int index) const
{
    if(index < 0 || index >= static_cast<int>(m_events.size()))
    {
        throw std::out_of_range("index out of range");
    }
    return m_events[index];
}
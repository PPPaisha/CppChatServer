//------------------------------------------------------
// 文件名:epoll.h
// 创建日期:2026-11-5
// 创建者:pppaisha
// 文件描述:epoll事件驱动模型头文件
//------------------------------------------------------
#pragma once

#include <sys/epoll.h>
#include <vector>

class Epoll
{
public:
    explicit Epoll(int maxEvents = 64);
    ~Epoll();

    Epoll(const Epoll&) = delete;
    Epoll& operator=(const Epoll&) = delete;

    Epoll(Epoll&& other) = delete;
    Epoll& operator=(Epoll&& other) = delete;

    void add(int fd, unit_32_t events);
    void modify(int fd, unit_32_t events);
    void remove(int fd);

    int wait(int timeoutMs);

    const epoll_event& eventAt(int index) const;

private:
    int epfd = -1;
    std::vector<epoll_event> m_events;


};
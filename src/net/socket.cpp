//------------------------------------------------------
// 文件名:socket.cpp
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:网络套接字类实现文件
//------------------------------------------------------
#include "net/socket.h"

#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <utility>

Socket::Socket()
{
    m_fd = ::socket(AF_INET,SOCK_STREAM,0);
    if(m_fd == -1)
    {
        throw std::runtime_error("Failed to create socket");
    }
}

Socket::Socket(int fd) : m_fd(fd)
{
    if(m_fd < 0)
    {
        throw std::invalid_argument("Invalid socket fd");
    }
}

Socket::~Socket()
{
    if(m_fd >= 0)
    {
        ::close(m_fd);
    }
}

Socket::Socket(Socket&& other) noexcept : m_fd(other.m_fd)
{
    other.m_fd = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept
{
    if(this != &other)
    {
        if(m_fd >= 0)
        {
            ::close(m_fd);
        }
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

int Socket::getFd() const noexcept
{
    return m_fd;
}

bool Socket::valid() const noexcept
{
    return m_fd >= 0;
}
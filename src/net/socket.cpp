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
#include <fcntl.h>
#include <cerrno>
#include <system_error>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>

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

void Socket::setNonBlocking()
{
    int flags = ::fcntl(m_fd, F_GETFL,0);
    if(flags == -1)
    {
        throw std::system_error(errno,std::generic_category(),"fcntl F_GETFL failed");
    }
    if(::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        throw std::system_error(errno, std::generic_category(),"fcntl F_SETFL failed");
    }
}

void Socket::setReuseAddr(bool enabled)
{
    int opt = enabled ? 1:0;
    if(::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        throw std::system_error(errno, std::generic_category(),"setsockopt SO_REUSEADDR failed");
    }
}

void Socket::bind(unsigned short port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
    addr.sin_port = ::htons(port);
    if(::bind(m_fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) == -1)
    {
        throw std::system_error(errno, std::generic_category(),"bind failed");
    }
}

void Socket::listen(int backlog)
{
    if(backlog < 0)
    {
        throw std::invalid_argument("backlog must not be negative");
    }
    if(::listen(m_fd,backlog) == -1)
    {
        throw std::system_error(errno, std::generic_category(),"listen failed");
    }
}

std::optional<Socket> Socket::accept()
{
    while(true)
    {
        int clientFd = ::accept4(m_fd,nullptr,nullptr,SOCK_NONBLOCK);
        if(clientFd >= 0)
        {
            return Socket(clientFd);
        }
        int error = errno;
        if(error == EINTR)
        {
            continue;
        }
        if(error == EAGAIN || error == EWOULDBLOCK)
        {
            return std::nullopt;
        }
        throw std::system_error(error, std::generic_category(),"accept failed");
    }
}
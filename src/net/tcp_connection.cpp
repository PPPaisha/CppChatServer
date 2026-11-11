//------------------------------------------------------
// 文件名:tcp_connection.cpp
// 创建日期:2026-11-9
// 创建者:pppaisha
// 文件描述:tcp连接类实现文件
//------------------------------------------------------
#include "net/tcp_connection.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <cerrno>
#include <system_error>
#include <utility>
#include <stdexcept>
#include <vector>

TcpConnection::TcpConnection(EventLoop& loop, Socket socket)
    :m_loop(loop),m_socket(std::move(socket))
{
}

void TcpConnection::start()
{
    auto weak = weak_from_this();
    m_loop.add(m_socket.getFd(),EPOLLIN,[weak](uint32_t events)
    {
        if(auto self = weak.lock())
        {
            self->handleRead(events);
            if(!self->m_closed && (events & EPOLLOUT))
            {
                self->handleWrite();
            }
        }
    });
}

void TcpConnection::handleRead(uint32_t events)
{
    if(events & EPOLLERR)
    {
        close();
        return;
    }
    if(!(events & (EPOLLIN | EPOLLHUP | EPOLLRDHUP)))
    {
        return;
    }
    char buffer[4096];

    while(true)
    {
        ssize_t n = ::recv(m_socket.getFd(),buffer,sizeof(buffer),0);
        if(n > 0)
        {
            std::vector<std::string> messages;
            try
            {
                messages = m_codec.feed(buffer,static_cast<std::size_t>(n));
            }
            catch(const std::length_error&)
            {
                close();
                return;
            }
            for(const auto& message : messages)
            {
                if(m_messageCallback)
                {
                    m_messageCallback(message);
                }
                if(m_closed)
                {
                    return;
                }
            }
        }
        else if(n == 0)
        {
            close();
            return;
        }
        else if(errno == EINTR)
        {
            continue;
        }
        else if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return;
        }
        else
        {
            close();
            return;
        }
    }
}

void TcpConnection::close()
{
    if(m_closed)
    {
        return;
    }
    m_closed = true;
    int fd = m_socket.getFd();
    m_loop.remove(fd);
    if(m_closeCallback)
    {
        m_closeCallback(fd);
    }
}

void TcpConnection::setMessageCallback(MessageCallback callback)
{
    m_messageCallback = std::move(callback);
}

void TcpConnection::setCloseCallback(CloseCallback callback)
{
    m_closeCallback = std::move(callback);
}

int TcpConnection::getFd() const noexcept
{
    return m_socket.getFd();
}

void TcpConnection::sendMessage(const std::string& message)
{
    if(m_closed)
    {
        return;
    }
    std::string frame = LengthFieldCodec::encode(message);
    m_outputBuffer.append(frame);
    handleWrite();
}

void TcpConnection::handleWrite()
{
    if(m_closed)
    {
        return;
    }

    while(m_outputoffset < m_outputBuffer.size())
    {
        const char* data = m_outputBuffer.data()+m_outputoffset;
        std::size_t remaining = m_outputBuffer.size()-m_outputoffset;
        ssize_t n = ::send(m_socket.getFd(),data,remaining,MSG_NOSIGNAL);
        if(n > 0)
        {
            m_outputoffset += static_cast<std::size_t>(n);
        }
        else if(n < 0 && errno == EINTR)
        {
            continue;
        }
        else if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            updateEvents();
            return;
        }
        else
        {
            close();
            return;
        }
    }
    m_outputBuffer.clear();
    m_outputoffset = 0;
    updateEvents();
}

//根据发送缓冲区状态 决定是否监听EPOLLOUT事件
void TcpConnection::updateEvents()
{
    if(m_closed)
    {
        return;
    }
    uint32_t events = EPOLLIN;
    if(m_outputoffset < m_outputBuffer.size())
    {
        events |= EPOLLOUT;
    }
    m_loop.modify(m_socket.getFd(),events);
}
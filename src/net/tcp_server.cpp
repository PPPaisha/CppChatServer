//------------------------------------------------------
// 文件名:tcp_server.cpp
// 创建日期:2026-11-12
// 创建者:pppaisha
// 文件描述:tcpserver类实现文件
//------------------------------------------------------
#include "net/tcp_server.h"

#include <sys/epoll.h>

#include <stdexcept>
#include <utility>

TcpServer::TcpServer(EventLoop& loop, uint16_t port)
    :m_loop(loop)
{
    m_listenSocket.setReuseAddr(true);
    m_listenSocket.setNonBlocking();
    m_listenSocket.bind(port);
    m_listenSocket.listen();
}

TcpServer::~TcpServer()
{
    if(m_started)
    {
        m_loop.remove(m_listenSocket.getFd());
    }
    //必须在EventLoop已停止 且没有回调执行时析构
    while(!m_connections.empty())
    {
        auto connection = m_connections.begin()->second;
        connection->close();
    }
}

void TcpServer::start()
{
    if(m_started)
    {
        throw std::logic_error("TcpServer already started");
    }
    m_loop.add(m_listenSocket.getFd(),EPOLLIN,[this](uint32_t events)
    {
        handleAccept(events);
    });
    m_started = true;
}

void TcpServer::handleAccept(uint32_t events)
{
    if(!(events & EPOLLIN))
    {
        return;
    }
    while(auto accepted = m_listenSocket.accept())
    {
        auto connection = std::make_shared<TcpConnection>(m_loop,std::move(*accepted));
        int fd = connection->getFd();
        std::weak_ptr<TcpConnection> weak = connection;
        connection->setMessageCallback([this,weak](const std::string& message)
    {
        if(auto self = weak.lock())
        {
            if(m_messageCallback)
            {
                m_messageCallback(self,message);
            }
        }
    });
    connection->setCloseCallback([this](int closedFd)
    {
        removeConnection(closedFd);
    });
    m_connections.emplace(fd,connection);
    connection->start();
    }
}

void TcpServer::removeConnection(int fd)
{
    m_connections.erase(fd);
    if(m_connectionCloseCallback)
    {
        m_connectionCloseCallback(fd);
    }
}

void TcpServer::setMessageCallback(MessageCallback callback)
{
    m_messageCallback = std::move(callback);
}

void TcpServer::setConnectionCloseCallback(ConnectionCloseCallback callback)
{
    m_connectionCloseCallback = std::move(callback);
}

std::size_t TcpServer::connectionCount() const noexcept
{
    return m_connections.size();
}
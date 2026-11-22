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
#include <exception>
#include <vector>

TcpServer::TcpServer(EventLoop& loop, uint16_t port,std::size_t ioThreadCount)
    :m_loop(loop),m_ioThreadPool(loop),m_ioThreadCount(ioThreadCount)
{
    m_listenSocket.setReuseAddr(true);
    m_listenSocket.setNonBlocking();
    m_listenSocket.bind(port);
    m_listenSocket.listen();
}

TcpServer::~TcpServer()
{
    if(m_started && !m_stopped)
    {
        std::terminate();
    }
}

void TcpServer::start()
{
    if(m_started)
    {
        throw std::logic_error("TcpServer already started");
    }
    m_ioThreadPool.start(m_ioThreadCount);
    m_loop.add(m_listenSocket.getFd(),EPOLLIN,[this](uint32_t events)
    {
        handleAccept(events);
    });
    m_started = true;
}

void TcpServer::removeConnection(int fd, const std::shared_ptr<TcpConnection>& connection)
{
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("removeConnection must be called in the loop thread");
    }
    auto it = m_connections.find(fd);
    if(it == m_connections.end() || it->second != connection)
    {
        return;
    }
    auto self = it->second;
    m_connections.erase(it);
    m_connectionLoops.erase(fd);
    if(m_connectionCloseCallback)
    {
        m_connectionCloseCallback(fd);
    }
    tryFinishStop();
}

void TcpServer::handleAccept(uint32_t events)
{
    if(m_stopping || !(events & EPOLLIN))
    {
        return;
    }
    while(auto accepted = m_listenSocket.accept())
    {
        EventLoop* ioLoop  = m_ioThreadPool.getNextLoop();
        auto pendingSocket = std::make_shared<Socket>(std::move(*accepted));
        ++m_pendingConnections;
        ioLoop->runInLoop([this,ioLoop,pendingSocket]()
        {
            establishConnection(ioLoop,pendingSocket);
        });
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

void TcpServer::broadcast(int senderFd, const std::string& message)
{
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("broadcast must be called in the loop thread");
    }
    if(m_stopping)
    {
        return;
    }
    //先保存需要广播的目标连接
    std::vector<std::shared_ptr<TcpConnection>> targets;
    for(const auto& [fd,connection] : m_connections)
    {
        if(fd  == senderFd || !connection)
        {
            continue;
        }
        targets.push_back(connection);
    }
    //再逐个发送 避免遍历m_connections时删除元素
    for(const auto& connection : targets)
    {
        connection->sendMessageAsync(message);
    }
}

void TcpServer::establishConnection(EventLoop* ioLoop, const std::shared_ptr<Socket>& pendingSocket)
{
    auto connection = std::make_shared<TcpConnection>(*ioLoop, std::move(*pendingSocket));
    const int fd = connection->getFd();
    std::weak_ptr<TcpConnection> weak = connection;
    //设置消息回调
    connection->setMessageCallback([this,weak,fd](const std::string& message)
    {
        if(auto self = weak.lock())
        {
            m_loop.runInLoop([this,self,message,fd]()
            {
                auto it = m_connections.find(fd);
                if( m_stopping || it == m_connections.end() || it->second != self)
                {
                    return;
                }
                if(m_messageCallback)
                {
                    m_messageCallback(self,message);
                }
            });
        }
    });

    //设置连接关闭回调
    connection->setCloseCallback([this,weak](int closedFd)
    {
        if(auto self = weak.lock())
        {
            m_loop.runInLoop([this,self,closedFd]()
            {
                removeConnection(closedFd,self);
            });
        }
    });

    //在所属I/O线程注册网络事件
    connection->start();

    m_loop.runInLoop([this,fd,connection,ioLoop]()
    {
        auto it = m_connections.find(fd);
        if(it != m_connections.end())
        {
            auto oldConnection = it->second;
            removeConnection(fd,oldConnection);
        }
        m_connections.emplace(fd,connection);
        m_connectionLoops.emplace(fd,ioLoop);
        --m_pendingConnections;
        if(m_stopping)
        {
            connection->closeAsync();
        }
        tryFinishStop();
    });
}

void TcpServer::stop()
{
    if(!m_loop.isInLoopThread())
    {
        m_loop.queueInLoop([this]()
        {
            stop();
        });
        return;
    }
    if(!m_started || m_stopping || m_stopped)
    {
        return;
    }
    m_stopping = true;
    //注销监听事件
    m_loop.remove(m_listenSocket.getFd());
    //保存当前链接快照
    std::vector<std::shared_ptr<TcpConnection>> targets;
    for(const auto& [fd,connection] : m_connections)
    {
        targets.push_back(connection);
    }
    //通知各个连接关闭
    for(const auto& connection:targets)
    {
        connection->closeAsync();
    }
    tryFinishStop();
}

void TcpServer::tryFinishStop()
{
    if(!m_stopping || m_stopped || m_finishStopQueued)
    {
        return;
    }
    if(m_pendingConnections != 0)
    {
        return;
    }
    if(!m_connections.empty())
    {
        return;
    }
    m_finishStopQueued = true;
    m_loop.queueInLoop([this]()
    {
        m_ioThreadPool.stop();
        m_stopped = true;
        m_loop.stop();
    });
}

bool TcpServer::isStopping() const
{
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("isStopping must be called in the loop thread");
    }
    return m_stopping;
}
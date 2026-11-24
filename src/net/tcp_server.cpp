//------------------------------------------------------
// 文件名:tcp_server.cpp
// 创建日期:2026-11-12
// 创建者:pppaisha
// 文件描述:tcpserver类实现文件
//------------------------------------------------------
#include "net/tcp_server.h"

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <stdexcept>
#include <utility>
#include <exception>
#include <vector>
#include <cerrno>
#include <chrono>
#include <system_error>

TcpServer::TcpServer(EventLoop& loop, uint16_t port,std::size_t ioThreadCount,std::chrono::seconds idleTimeout)
    :m_loop(loop),m_ioThreadPool(loop),m_ioThreadCount(ioThreadCount),m_idleTimeout(idleTimeout)
{
    if(m_idleTimeout.count() <= 0)
    {
        throw std::invalid_argument("idleTimeout must be positive");
    }
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
    if(m_timerFd >= 0)
    {   
        ::close(m_timerFd);
        m_timerFd = -1;
    }
}

void TcpServer::start()
{
    if(m_started)
    {
        throw std::logic_error("TcpServer already started");
    }
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("TcpServer::start must run in main loop thread");
    }
    m_timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(m_timerFd == -1)
    {
        throw std::system_error(errno, std::generic_category(), "timerfd_create failed");
    }
    itimerspec timer{};
    timer.it_value.tv_sec = 1;
    timer.it_interval.tv_sec = 1;
    if(::timerfd_settime(m_timerFd,0,&timer,nullptr) == -1)
    {
        int error = errno;
        ::close(m_timerFd);
        m_timerFd = -1;
        throw std::system_error(error, std::generic_category(), "timerfd_settime failed");
    }
    try
    {
        m_loop.add(m_timerFd,EPOLLIN,[this](uint32_t events)
        {
            handleTimer(events);
        });
    }
    catch(...)
    {
        ::close(m_timerFd);
        m_timerFd = -1;
        throw;
    }

    try
    {
        m_ioThreadPool.start(m_ioThreadCount);
        m_loop.add(m_listenSocket.getFd(),EPOLLIN,[this](uint32_t events)
        {
            handleAccept(events);
        });
    }
    catch(...)
    {
        stopTimer();
        m_ioThreadPool.stop();
        throw;
    }
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
    m_lastActiveTime.erase(fd);
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
                m_lastActiveTime[fd] = Clock::now();
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
        m_lastActiveTime[fd] = Clock::now();
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
    stopTimer();
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

void TcpServer::handleTimer(uint32_t events)
{
    if(!(events & EPOLLIN))
    {
        return;
    }
    uint64_t expirations = 0;
    while(true)
    {
        ssize_t n = ::read(m_timerFd,&expirations,sizeof(expirations));
        if(n == sizeof(expirations))
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
        throw std::system_error(n == -1 ? errno : EIO, std::generic_category(), "timerfd read failed");
    }
    if(m_stopping)
    {return;}
    const auto now = Clock::now();
    std::vector<ConnectionPtr> expiredConnections;
    for(auto& [fd,lastActive] : m_lastActiveTime)
    {
        if(now-lastActive < m_idleTimeout)
        {
            continue;
        }
        auto it = m_connections.find(fd);
        if(it == m_connections.end())
        {
            continue;
        }
        expiredConnections.push_back(it->second);
        //避免下一次定时扫描重复提交关闭任务
        lastActive = now;
    } 
    for(const auto& connection : expiredConnections)
    {
        connection->closeAsync();
    }
}

void TcpServer::stopTimer()
{
    if(m_timerFd < 0)
    {
        return;
    }
    m_loop.remove(m_timerFd);
    ::close(m_timerFd);
    m_timerFd = -1;
}
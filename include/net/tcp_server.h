//------------------------------------------------------
// 文件名:tcp_server.h
// 创建日期:2026-11-12
// 创建者:pppaisha
// 文件描述:tcpserver类头文件
//------------------------------------------------------
#pragma once

#include "net/event_loop_thread_pool.h"
#include "net/socket.h"
#include "net/tcp_connection.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <chrono>
#include <string>
#include <unordered_map>

class TcpServer
{
public:
    using ConnectionPtr = std::shared_ptr<TcpConnection>;
    using MessageCallback = std::function<void(const ConnectionPtr&, const std::string&)>;
    using ConnectionCloseCallback = std::function<void(int)>;

    TcpServer(EventLoop& loop, uint16_t port,size_t ioThreadCount = 0,std::chrono::seconds idleTimeout = std::chrono::seconds(30));
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    ~TcpServer();

    void start();
    void stop();
    void setMessageCallback(MessageCallback callback);
    void setConnectionCloseCallback(ConnectionCloseCallback callback);
    void broadcast(int senderFd, const std::string& message);//将消息广播给除senderFd之外的所有连接

    bool isStopping() const;
    
    std::size_t connectionCount() const noexcept;
private:
    void handleAccept(uint32_t events);
    void tryFinishStop();
    void establishConnection(EventLoop* ioLoop, const std::shared_ptr<Socket>& pendingSocket);
    void removeConnection(int fd,const std::shared_ptr<TcpConnection>& connection);
    void handleTimer(uint32_t events);
    void stopTimer();

    EventLoop& m_loop;
    EventLoopThreadPool m_ioThreadPool;
    Socket m_listenSocket;

    std::unordered_map<int,ConnectionPtr> m_connections;
    std::unordered_map<int,EventLoop*> m_connectionLoops;
    std::size_t m_ioThreadCount;
    std::size_t m_pendingConnections = 0;
    MessageCallback m_messageCallback;
    ConnectionCloseCallback m_connectionCloseCallback;
    bool m_started = false;
    bool m_stopping = false;
    bool m_stopped = false;
    bool m_finishStopQueued = false;

    using Clock = std::chrono::steady_clock;
    std::unordered_map<int,Clock::time_point> m_lastActiveTime;
    std::chrono::seconds m_idleTimeout;
    int m_timerFd = -1;
};
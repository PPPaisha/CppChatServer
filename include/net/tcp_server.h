//------------------------------------------------------
// 文件名:tcp_server.h
// 创建日期:2026-11-12
// 创建者:pppaisha
// 文件描述:tcpserver类头文件
//------------------------------------------------------
#pragma once

#include "net/event_loop.h"
#include "net/socket.h"
#include "net/tcp_connection.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

class TcpServer
{
public:
    using ConnectionPtr = std::shared_ptr<TcpConnection>;
    using MessageCallback = std::function<void(const ConnectionPtr&, const std::string&)>;
    using ConnectionCloseCallback = std::function<void(int)>;

    TcpServer(EventLoop& loop, uint16_t port);
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    ~TcpServer();

    void start();
    void setMessageCallback(MessageCallback callback);
    void setConnectionCloseCallback(ConnectionCloseCallback callback);
    
    std::size_t connectionCount() const noexcept;
private:
    void handleAccept(uint32_t events);
    void removeConnection(int fd);

    EventLoop& m_loop;
    Socket m_listenSocket;

    std::unordered_map<int,ConnectionPtr> m_connections;
    MessageCallback m_messageCallback;
    bool m_started = false;
};
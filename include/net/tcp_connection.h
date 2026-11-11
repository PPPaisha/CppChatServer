//------------------------------------------------------
// 文件名:tcp_connection.h
// 创建日期:2026-11-9
// 创建者:pppaisha
// 文件描述:tcp连接类头文件
//------------------------------------------------------
#pragma once

#include "net/socket.h"
#include "net/event_loop.h"
#include "net/length_field_codec.h"

#include <functional>
#include <memory>
#include <string>

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void(int)>;

    TcpConnection(EventLoop& loop, Socket socket);
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void start();
    void close();
    void setMessageCallback(MessageCallback callback);
    void setCloseCallback(CloseCallback callback);
    int getFd() const noexcept;

private:
    void handleRead(uint32_t events);
    EventLoop& m_loop;
    Socket m_socket;
    MessageCallback m_messageCallback;
    CloseCallback m_closeCallback;
    bool m_closed = false;
    LengthFieldCodec m_codec;
};
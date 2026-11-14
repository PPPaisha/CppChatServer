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
#include <cstddef>

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void(int)>;
    static constexpr std::size_t MAX_PENDING_BYTES = 1024*1024;

    TcpConnection(EventLoop& loop, Socket socket);
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void start();
    void close();
    void setMessageCallback(MessageCallback callback);
    void setCloseCallback(CloseCallback callback);
    void sendMessage(const std::string& message);
    bool trySendMessage(const std::string& message);
    void shutdown();
    int getFd() const noexcept;
    std::size_t pendingBytes() const noexcept;

private:
    void handleRead(uint32_t events);
    void handleWrite();
    void updateEvents();
    void shutdownWrite();
    EventLoop& m_loop;
    Socket m_socket;
    MessageCallback m_messageCallback;
    CloseCallback m_closeCallback;
    bool m_closed = false; //表示连接已经进入最终关闭流程
    bool m_disconnecting = false;//表示已经请求优雅关闭 不再接受新的发送消息
    bool m_writeShutdown = false;//表示已经成功执行写方向半关闭
    bool m_peerReadClosed = false;//表示已经收到了对端的EOF
    LengthFieldCodec m_codec;
    std::string m_outputBuffer;
    std::size_t m_outputoffset = 0;
};
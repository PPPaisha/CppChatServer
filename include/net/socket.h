//------------------------------------------------------
// 文件名:socket.h
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:网络套接字类头文件
//------------------------------------------------------
#pragma once

#include <optional>

class Socket
{
public:
    Socket();
    explicit Socket(int fd);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    int getFd() const noexcept;
    bool valid() const noexcept;

    void setNonBlocking();//设置套接字为非阻塞模式
    void setReuseAddr(bool enabled);//设置套接字地址重用

    void bind(unsigned short port);//绑定套接字到指定端口
    void listen(int backlog = 128);//监听套接字

    std::optional<Socket> accept();

private:
    int m_fd = -1;
};

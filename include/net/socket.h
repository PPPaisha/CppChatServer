//------------------------------------------------------
// 文件名:socket.h
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:网络套接字类头文件
//------------------------------------------------------
#pragma once

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

private:
    int m_fd = -1;
};
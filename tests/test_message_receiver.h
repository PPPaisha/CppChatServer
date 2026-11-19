//------------------------------------------------------
// 文件名:tcp_message_receiver.h
// 创建日期:2026-11-19
// 创建者:pppaisha
// 文件描述:持久化消息接收器头文件
//------------------------------------------------------
#pragma once

#include "net/length_field_codec.h"

#include <poll.h>
#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>

class TestMessageReceiver
{
public:
    explicit TestMessageReceiver(int fd)
        :m_fd(fd)
    {
    }

    std::string receiveMessage()
    {
        if(!m_messages.empty())
        {
            return popMessage();
        }
        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        char buffer[4096];
        while(Clock::now() < deadline)
        {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            int timeout = static_cast<int>(remaining.count());
            if(timeout <= 0)
            {
                timeout = 1;
            }
            pollfd pfd{};
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            int ready = ::poll(&pfd,1,timeout);
            if(ready < 0)
            {
                if(errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("poll failed");
            }
            if(ready == 0)
            {
                continue;
            }
            if(pfd.revents & POLLNVAL)
            {
                throw std::runtime_error("Invalid socket");
            }
            if(pfd.revents & POLLERR)
            {
                throw std::runtime_error("Socket error");
            }
            if(!(pfd.revents & (POLLIN | POLLHUP)))
            {
                continue;
            }
            ssize_t n = ::recv(m_fd,buffer,sizeof(buffer),0);
            if(n == 0)
            {
                throw std::runtime_error("Peer closed");
            }
            if(n < 0)
            {
                if(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                throw std::runtime_error("recv failed");
            }
            auto decoded = m_codec.feed(buffer,static_cast<std::size_t>(n));
            for(auto& message : decoded)
            {
                m_messages.push_back(std::move(message));
            }
            if(!m_messages.empty())
            {
                return popMessage();
            }
        }
        throw std::runtime_error("Receive timeout");
    }

private:
    std::string popMessage()
    {
        std::string result = std::move(m_messages.front());
        m_messages.pop_front();
        return result;
    }

    int m_fd;
    LengthFieldCodec m_codec;
    std::deque<std::string> m_messages;
};
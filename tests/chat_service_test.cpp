//------------------------------------------------------
// 文件名:chat_service_test.cpp
// 创建日期:2026-11-18
// 创建者:pppaisha
// 文件描述:客户端聊天服务测试文件
//------------------------------------------------------
#include "chat/chat_service.h"
#include "net/event_loop.h"
#include "net/tcp_server.h"
#include "net/length_field_codec.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
#include <future>
#include <cstdint>
#include <exception>
#include <thread>



void sendAll(int fd, const std::string& data)
{
    std::size_t offset = 0;
    while(offset < data.size())
    {
        ssize_t n = ::send(fd,data.data()+offset, data.size()-offset,MSG_NOSIGNAL);
        if(n>0)
        {
            offset += static_cast<std::size_t>(n);
        }
        else if(n<0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            throw std::runtime_error("send failed");
        }
    }
}

int connectClient(uint16_t port)
{
    int fd = ::socket(AF_INET,SOCK_STREAM,0);
    if(fd < 0)
    {
        throw std::runtime_error("socket failed");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if(::connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) != 0)
    {
        ::close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

std::string receiveMessage(int fd)
{
    LengthFieldCodec decoder;
    char buffer[4096];

    using Clock = std::chrono::steady_clock;

    const auto deadline =
        Clock::now() + std::chrono::seconds(3);

    while (true)
    {
        auto now = Clock::now();

        if (now >= deadline)
        {
            throw std::runtime_error("Receive timeout");
        }

        auto remaining =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(deadline - now);

        int timeout = static_cast<int>(
            remaining.count()
        );

        if (timeout == 0)
        {
            timeout = 1;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        int ready = ::poll(&pfd, 1, timeout);

        if (ready == 0)
        {
            throw std::runtime_error("poll timeout");
        }

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            throw std::runtime_error("poll failed");
        }

        if (pfd.revents & POLLNVAL)
        {
            throw std::runtime_error("Invalid socket");
        }

        if (pfd.revents & POLLERR)
        {
            throw std::runtime_error("Socket error");
        }

        if (!(pfd.revents & (POLLIN | POLLHUP)))
        {
            continue;
        }

        ssize_t n = ::recv(
            fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (n == 0)
        {
            throw std::runtime_error(
                "Peer closed connection"
            );
        }

        if (n < 0)
        {
            if (errno == EINTR ||
                errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                continue;
            }

            throw std::runtime_error("recv failed");
        }

        auto messages = decoder.feed(
            buffer,
            static_cast<std::size_t>(n)
        );

        if (!messages.empty())
        {
            return messages.front();
        }
    }
}

std::string encodeFrame(const std::string& message)
{
    uint32_t length = htonl(
        static_cast<uint32_t>(message.size())
    );

    return std::string(
        reinterpret_cast<const char*>(&length),
        sizeof(length)
    ) + message;
}

void sendCommand(int fd, const std::string& command)
{
    sendAll(fd,encodeFrame(command));
}

void expectMessage(int fd, const std::string& expected)
{
    std::string actual = receiveMessage(fd);
    if(actual != expected)
    {
        throw std::runtime_error(
            "Expected message: " + expected +
            ", but received: " + actual
        );
    }
}

std::size_t queryOnlineCount(EventLoop& loop, ChatService& chat)
{
    auto promise = std::make_shared<std::promise<std::size_t>>();
    auto future = promise->get_future();
    loop.queueInLoop([&chat,promise]()
    {
        promise->set_value(chat.onlineCount());
    });
    if(future.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
    {
        throw std::runtime_error("Timeout waiting for online count");
    }
    return future.get();
}

int main()
{
    constexpr uint16_t PORT = 12081;
    EventLoop loop;
    TcpServer server(loop, PORT);
    ChatService chat(loop);
    server.setMessageCallback([&chat](const std::shared_ptr<TcpConnection>&conn,const std::string& message)
    {
        chat.handleMessage(conn,message);
    });
    server.setConnectionCloseCallback([&chat](int fd)
    {
        chat.removeConnection(fd);
    });
    server.start();

    std::exception_ptr workerError;
    std::thread worker([&]()
    {
        int a = -1;
        int b = -1;
        int c = -1;
        try
        {
            a = connectClient(PORT);
            b = connectClient(PORT);
            c = connectClient(PORT);
            //1.未登录发送消息
            sendCommand(a,"SEND Bob Hello");
            expectMessage(a,"ERROR Not logged in");

            //2.三个用户注册
            sendCommand(a,"LOGIN Alice");
            expectMessage(a,"LOGIN_OK");
            sendCommand(b,"LOGIN Bob");
            expectMessage(b,"LOGIN_OK");
            sendCommand(c,"LOGIN Charlie");
            expectMessage(c,"LOGIN_OK");
            if(queryOnlineCount(loop,chat) != 3)
            {
                throw std::runtime_error("Online count should be 3");
            }
            std::cout<<"Registration passed!"<<std::endl;

            //3.重复注册
            sendCommand(a,"LOGIN Alice");
            expectMessage(a,"LOGIN_FAIL");
            sendCommand(b,"LOGIN David");
            expectMessage(b,"LOGIN_FAIL");
            std::cout<<"Duplicate login passed!"<<std::endl;

            //4.Alice私聊Bob
            sendCommand(a,"SEND Bob Hello");
            expectMessage(b,"FROM Alice Hello");
            expectMessage(a,"SEND_OK");
            std::cout<<"Private message passed!"<<std::endl;

            //5.Alice广播
            sendCommand(a,"BROADCAST Hi all");
            expectMessage(b,"BROADCAST_FROM Alice Hi all");
            expectMessage(c,"BROADCAST_FROM Alice Hi all");
            expectMessage(a,"BROADCAST_OK");
            std::cout<<"Broadcast passed!"<<std::endl;

            //6.Bob断开连接
            ::close(b);
            b = -1;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while(queryOnlineCount(loop,chat) != 2)
            {
                if(std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("Bob was not removed");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            sendCommand(a,"SEND Bob Hello");
            expectMessage(a,"SEND_FAILED");
            std::cout<<"Disconnect cleanup passed!"<<std::endl;

            //7.其他客户端断开
            ::close(a);
            ::close(c);
            a = c = -1;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while(queryOnlineCount(loop,chat) != 0)
            {
                if(std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("Sessions were not cleaned up");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::cout<<"All sessions cleaned up!"<<std::endl;
        }
        catch(...)
        {
            workerError = std::current_exception();
        }
        if(a >= 0)
        {
            ::close(a);
        }
        if(b >= 0)
        {
            ::close(b);
        }
        if(c >= 0)
        {
            ::close(c);
        }
        loop.stop();
    });
    loop.run();
    worker.join();
    if(workerError)
    {
        try
        {
            std::rethrow_exception(workerError);
        }
        catch(const std::exception& e)
        {
            std::cerr<<"Test failed: "<<e.what()<<std::endl;
            return 1;
        }
    }
    std::cout<<"Version2.1 test passed!"<<std::endl;
    return 0;
}
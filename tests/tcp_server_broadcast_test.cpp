//------------------------------------------------------
// 文件名:tcp_server_broadcast_test.cpp
// 创建日期:2026-11-9
// 创建者:pppaisha
// 文件描述:tcp服务器广播功能测试文件
//------------------------------------------------------
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

std::size_t queryConnectionCount(EventLoop& loop, TcpServer& server)
{
    auto promise = std::make_shared<std::promise<std::size_t>>();

    auto future = promise->get_future();

    loop.queueInLoop([&server, promise]()
    {
        promise->set_value(server.connectionCount());
    });

    if (future.wait_for(std::chrono::seconds(3))
        != std::future_status::ready)
    {
        throw std::runtime_error(
            "Connection count query timeout"
        );
    }

    return future.get();
}

int main()
{
    constexpr uint16_t PORT = 12080;
    EventLoop loop;
    TcpServer server(loop, PORT);
    server.start();
    std::exception_ptr workerError;
    std::thread worker([&]()
    {
        int clientA = -1;
        int clientB = -1;
        int clientC = -1;
        try
        {
            clientA = connectClient(PORT);
            clientB = connectClient(PORT);
            clientC = connectClient(PORT);
            //等待服务器完成三个连接的accept
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while(queryConnectionCount(loop, server) != 3)
            {
                if(std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("Timeout waiting for connections");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::cout<<"Three clients connected!"<<std::endl;
            //A发送 B C接收
            sendAll(clientA, encodeFrame("Hello"));
            if(receiveMessage(clientB) != "Hello" || receiveMessage(clientC) != "Hello")
            {
                throw std::runtime_error("Client B or C did not receive the correct message");
            }
            std::cout<<"First broadcast test passed!"<<std::endl;
            //验证A没有收到自己的广播
            pollfd pfd{};
            pfd.fd = clientA;
            pfd.events = POLLIN;
            int ready = ::poll(&pfd, 1, 200);
            if(ready != 0)
            {
                throw std::runtime_error("Client A received its own broadcast");
            }
            std::cout<<"Sender exclusion passed!"<<std::endl;
            //B断开连接
            ::close(clientB);
            clientB = -1;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);

            while(queryConnectionCount(loop, server) != 2)
            {
                if(std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("Timeout waiting for connection removal");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            //B断开后 A再次向C发送广播
            sendAll(clientA,encodeFrame("World"));
            if(receiveMessage(clientC) != "World")
            {
                throw std::runtime_error("Client C did not receive the correct message after B disconnected");
            }
            std::cout<<"broadcast after disconnection test passed!"<<std::endl;
            ::close(clientA);
            ::close(clientC);
            clientA = clientC = -1;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while(queryConnectionCount(loop, server) != 0)
            {
                if(std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("Timeout waiting for all connections to close");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        catch(...)
        {
            workerError = std::current_exception();
        }
        if(clientA >= 0)
        {
            ::close(clientA);
        }
        if(clientB >= 0)
        {
            ::close(clientB);
        }
        if(clientC >= 0)
        {
            ::close(clientC);
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
            std::cerr<<"Test failed:"<<e.what()<<std::endl;
            return 1;
        }
    }
    std::cout<<"TcpServer broadcast test passed!"<<std::endl;
    return 0;
}
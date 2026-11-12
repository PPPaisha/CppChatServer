//------------------------------------------------------
// 文件名:tcp_connection_shutdown_test.cpp
// 创建日期:2026-11-12
// 创建者:pppaisha
// 文件描述:tcp连接关闭测试
//------------------------------------------------------
#include "net/tcp_connection.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <cassert>
#include <cerrno>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

void sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;

    while (sent < data.size())
    {
        ssize_t n = ::send(
            fd, data.data() + sent,
            data.size() - sent, MSG_NOSIGNAL
        );

        if (n > 0)
            sent += static_cast<std::size_t>(n);
        else if (n < 0 && errno == EINTR)
            continue;
        else
            throw std::runtime_error("send failed");
    }
}

void runTest(bool clientHalfClosesFirst)
{
    EventLoop loop;
    Socket server;

    server.setReuseAddr(true);
    server.setNonBlocking();
    server.bind(0);
    server.listen();

    sockaddr_in address{};
    socklen_t length = sizeof(address);

    if (::getsockname(
            server.getFd(),
            reinterpret_cast<sockaddr*>(&address),
            &length) != 0)
    {
        throw std::runtime_error("getsockname failed");
    }

    int port = ntohs(address.sin_port);

    std::unordered_map<int, std::shared_ptr<TcpConnection>>
        connections;

    const std::string expected(128 * 1024, 'A');

    loop.add(server.getFd(), EPOLLIN, [&](uint32_t)
    {
        while (auto accepted = server.accept())
        {
            auto connection = std::make_shared<TcpConnection>(
                loop, std::move(*accepted)
            );

            int fd = connection->getFd();
            std::weak_ptr<TcpConnection> weak = connection;

            connection->setMessageCallback(
                [weak, &expected](const std::string&)
                {
                    if (auto self = weak.lock())
                    {
                        self->sendMessage(expected);
                        self->shutdown();
                    }
                }
            );

            connection->setCloseCallback([&](int closedFd)
            {
                connections.erase(closedFd);
                loop.stop();
            });

            connections.emplace(fd, connection);
            connection->start();
        }
    });

    std::exception_ptr clientError;

    std::thread clientThread([&]()
    {
        try
        {
            Socket client;

            timeval timeout{};
            timeout.tv_sec = 5;

            if (::setsockopt(
                    client.getFd(), SOL_SOCKET, SO_RCVTIMEO,
                    &timeout, sizeof(timeout)) != 0)
            {
                throw std::runtime_error("setsockopt failed");
            }

            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);

            if (::connect(
                    client.getFd(),
                    reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) != 0)
            {
                throw std::runtime_error("connect failed");
            }

            sendAll(
                client.getFd(),
                LengthFieldCodec::encode("Request")
            );

            if (clientHalfClosesFirst)
                ::shutdown(client.getFd(), SHUT_WR);

            LengthFieldCodec decoder;
            std::vector<std::string> received;
            char buffer[4096];

            while (true)
            {
                ssize_t n = ::recv(
                    client.getFd(), buffer, sizeof(buffer), 0
                );

                if (n > 0)
                {
                    auto messages = decoder.feed(
                        buffer, static_cast<std::size_t>(n)
                    );

                    for (auto& message : messages)
                        received.push_back(std::move(message));
                }
                else if (n == 0)
                {
                    break;
                }
                else if (errno != EINTR)
                {
                    throw std::runtime_error("recv failed");
                }
            }

            if (received.size() != 1 ||
                received[0] != expected ||
                decoder.bufferedBytes() != 0)
            {
                throw std::runtime_error("response mismatch");
            }

            if (!clientHalfClosesFirst)
                ::shutdown(client.getFd(), SHUT_WR);
        }
        catch (...)
        {
            clientError = std::current_exception();
            loop.stop();
        }
    });

    loop.run();
    clientThread.join();

    if (clientError)
        std::rethrow_exception(clientError);

    assert(connections.empty());
}

int main()
{
    runTest(false);
    runTest(true);

    std::cout << "TcpConnection shutdown test passed!"<<std::endl;
}
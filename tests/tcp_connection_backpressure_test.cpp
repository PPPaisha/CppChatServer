//------------------------------------------------------
// 文件名:tcp_conneciton_backpressure_test.cpp
// 创建日期:2026-11-14
// 创建者:pppaisha
// 文件描述:背压测试
//------------------------------------------------------
#include "net/tcp_connection.h"
#include "net/length_field_codec.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <atomic>
#include <cerrno>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

int main()
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

    std::shared_ptr<TcpConnection> connection;

    std::atomic<bool> observedBackpressure{false};
    std::atomic<bool> observedRejection{false};

    loop.add(server.getFd(), EPOLLIN, [&](uint32_t)
    {
        while (auto accepted = server.accept())
        {
            int sendBufferSize = 4096;

            if (::setsockopt(
                accepted->getFd(),
                SOL_SOCKET,
                SO_SNDBUF,
                &sendBufferSize,
                sizeof(sendBufferSize)) != 0)
            {
                throw std::runtime_error("setsockopt failed");
            }

            connection = std::make_shared<TcpConnection>(
                loop, std::move(*accepted)
            );

            connection->setCloseCallback([&](int fd)
            {
                loop.stop();
            });

            connection->start();

            const std::string payload(64 * 1024, 'A');

            for (int i = 0; i < 100; ++i)
            {
                bool acceptedMessage =
                    connection->trySendMessage(payload);

                if (connection->pendingBytes() > 0)
                {
                    observedBackpressure = true;
                }

                if (!acceptedMessage)
                {
                    observedRejection = true;
                    break;
                }
            }
            connection->shutdown();
        }
       
    });

    std::exception_ptr clientError;

    std::thread clientThread([&]()
    {
        try
        {
            Socket client;

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

            // 暂时不读取服务器发送的数据，
            // 让内核接收缓冲区逐渐积压。
            std::this_thread::sleep_for(
                std::chrono::milliseconds(500)
            );

            char buffer[8192];

            while (true)
            {
                ssize_t n = ::recv(
                    client.getFd(),
                    buffer,
                    sizeof(buffer),
                    0
                );

                if (n > 0)
                {
                    continue;
                }

                if (n == 0)
                {
                    if(::shutdown(client.getFd(),SHUT_WR) != 0)
                    {
                        throw std::runtime_error("shutdown failed");
                    }
                    break;
                }

                if (errno == EINTR)
                {
                    continue;
                }

                throw std::runtime_error("recv failed");
            }
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
    {
        std::rethrow_exception(clientError);
    }

    if (!observedBackpressure)
    {
        throw std::runtime_error(
            "Backpressure was not observed"
        );
    }

    if (!observedRejection)
    {
        throw std::runtime_error(
            "Buffer limit rejection was not observed"
        );
    }
    connection.reset();
    std::cout << "TcpConnection backpressure test passed!\n";
}
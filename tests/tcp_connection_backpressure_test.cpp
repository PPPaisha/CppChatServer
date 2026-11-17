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
    EventLoop loop;//创建一个事件循环对象

    Socket server;//创建一个套接字对象
    server.setReuseAddr(true);//设置套接字地址复用
    server.setNonBlocking();//设置套接字为非阻塞模式
    server.bind(0);//绑定套接字到任意可用端口
    server.listen();//开启监听

    sockaddr_in address{};
    socklen_t length = sizeof(address);

    if (::getsockname(
        server.getFd(),
        reinterpret_cast<sockaddr*>(&address),
        &length) != 0)
    {
        throw std::runtime_error("getsockname failed");
    }

    int port = ntohs(address.sin_port);//拿到套接字的端口号

    std::shared_ptr<TcpConnection> connection;//创建一个智能指针来管理TcpConnection对象

    std::atomic<bool> observedBackpressure{false};
    std::atomic<bool> observedRejection{false};

    //将套接字server添加到事件循环中 监听其EPOLLIN事件 当有客户端连接时 触发回调函数
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
            //将serveraccept到的客户端套接字交给connection来管理
            connection = std::make_shared<TcpConnection>(
                loop, std::move(*accepted)
            );
            //设置连接关闭的回调函数 当连接关闭时 停止事件循环
            connection->setCloseCallback([&](int fd)
            {
                loop.stop();
            });
            //将这个连接添加到事件循环中
            //主要流程包括: 将这个客户端加入时间循环中 监听其EPOLLIN事件 当有数据可读时 触发回调函数
            connection->start();
            //服务器端准备发送的数据 64KB的'A'字符
            const std::string payload(64 * 1024, 'A');

            for (int i = 0; i < 100; ++i)
            {
                //记录发送数据的结果 如果发送缓冲区已满 则返回false 表示拒绝发送
                bool acceptedMessage =
                    connection->trySendMessage(payload);
                //如果发送缓冲区中有未发送的数据 则说明发生了背压
                if (connection->pendingBytes() > 0)
                {
                    observedBackpressure = true;
                }
                //如果发送缓冲区已满 则说明发生了拒绝发送
                if (!acceptedMessage)
                {
                    observedRejection = true;
                    break;
                }
            }
            // 所有发送请求结束，发起优雅关闭
            // 如果应用层还有待发送数据，继续等待 EPOLLOUT
            // 数据全部交给内核后，执行写半关闭
            // 最终连接清理由 TcpConnection::close() 完成
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
                //尝试接收服务器发送的数据
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
    //启动事件循环
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
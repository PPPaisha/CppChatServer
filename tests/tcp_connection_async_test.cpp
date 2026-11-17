//------------------------------------------------------
// 文件名:tcp_connection_async_test.cpp
// 创建日期:2026-11-17
// 创建者:pppaisha
// 文件描述:测试TCP连接的异步发送功能
//------------------------------------------------------
#include "net/tcp_connection.h"
#include "net/event_loop.h"
#include "net/socket.h"
#include "net/length_field_codec.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <memory>

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
    if(::getsockname(server.getFd(),reinterpret_cast<sockaddr*>(&address),&length)!=0)
    {
        throw std::runtime_error("getsockname failed");
    }

    const int port = ntohs(address.sin_port);
    constexpr int messageCount = 100;
    std::shared_ptr<TcpConnection> connection;
    std::thread producer;
    loop.add(server.getFd(),EPOLLIN,[&](uint32_t)
    {
        while(auto accepted = server.accept())
        {
            connection = std::make_shared<TcpConnection>(loop,std::move(*accepted));
            connection->setCloseCallback([&](int)
            {
                loop.stop();
            });
            connection->start();
            auto target = connection;
            producer = std::thread([target]()
            {
                for(int i=0; i<messageCount; ++i)
                {
                    target->sendMessageAsync("message-" + std::to_string(i));
                }
                target->sendMessageAsync("END");
            });
        }
    });

    std::thread clientThread([&]()
    {
        Socket client;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if(::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address)) != 0)
        {
            throw std::runtime_error("connect failed");
        }
        LengthFieldCodec decoder;
        int receivedCount = 0;
        bool receivedEnd = false;
        char buffer[4096];
        while(!receivedEnd)
        {
            ssize_t n = ::recv(client.getFd(),buffer,sizeof(buffer),0);
            if(n > 0)
            {
                auto messages = decoder.feed(buffer,static_cast<std::size_t>(n));
                for(const auto& message : messages)
                {
                    if(message == "END")
                    {
                        if(receivedCount != messageCount)
                        {
                            throw std::runtime_error("Incorrect message count");
                        }
                        receivedEnd = true;
                        break;
                    }
                    const std::string expected = "message-" + std::to_string(receivedCount);
                    if(message != expected)
                    {
                        throw std::runtime_error("Message order or content mismatch");
                    }
                    ++receivedCount;
                }
            }
            else if(n == 0)
            {
                throw std::runtime_error("Connection closed before END");
            }
            else if(errno != EINTR)
            {
                throw std::runtime_error("recv failed");
            }
        }
    });
    
    loop.run();
    if(producer.joinable())
    {
        producer.join();
    }
    clientThread.join();
    connection.reset();
    std::cout<<"TcpConnection async send test passed!"<<std::endl;
    return 0;
}
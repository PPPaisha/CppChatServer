//------------------------------------------------------
// 文件名:tcp_connection_send_test.cpp
// 创建日期:2026-11-11
// 创建者:pppaisha
// 文件描述:tcp连接类发送功能测试文件
//------------------------------------------------------
#include "net/tcp_connection.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cerrno>
#include <stdexcept>

void sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while(sent < data.size())
    {
        ssize_t n = ::send(fd,data.data()+sent,data.size()-sent,MSG_NOSIGNAL);
        if(n > 0)
        {
            sent += static_cast<std::size_t>(n);
        }
        else if(n < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            throw std::runtime_error("Client send failed");
        }
    }
}

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
    assert(::getsockname(server.getFd(),reinterpret_cast<sockaddr*>(&address),&length) == 0);
    int port = ntohs(address.sin_port);

    std::unordered_map<int,std::shared_ptr<TcpConnection>> connections;
    loop.add(server.getFd(),EPOLLIN,[&](uint32_t)
    {
        while(auto accepted = server.accept())
        {
            auto connection = std::make_shared<TcpConnection>(loop,std::move(*accepted));
            int fd = connection->getFd();
            std::weak_ptr<TcpConnection> weak = connection;
            connection->setMessageCallback([weak](const std::string& message)
            {
                if (auto self = weak.lock())
                {
                    self->sendMessage(message);
                }
            });
            connection->setCloseCallback([&](int closeFd)
            {
                connections.erase(closeFd);
                loop.stop();
            });
            connections.emplace(fd,connection);
            connection->start();
        }
    });

    std::thread clientThread([&]()
    {
        Socket client;
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0);
        const std::vector<std::string> expected = {"Hello","CppChatServer",std::string(128*1024,'A')};
        for(const auto& message : expected)
        {
            sendAll(client.getFd(),LengthFieldCodec::encode(message));
        }
        LengthFieldCodec decoder;
        std::vector<std::string> received;
        char buffer[4096];
        while(received.size() < expected.size())
        {
            ssize_t n = ::recv(client.getFd(),buffer,sizeof(buffer),0);
            if(n > 0)
            {
                auto messages = decoder.feed(buffer,static_cast<std::size_t>(n));
                for(auto& message : messages)
                {
                    received.push_back(std::move(message));
                }
            }
            else if(n < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                throw std::runtime_error("Client receive failed");
            }
        }
        assert(received == expected);
        ::shutdown(client.getFd(),SHUT_WR);
    });
    loop.run();
    clientThread.join();
    assert(connections.empty());
    std::cout<<"TcpConnection send test passed!"<<std::endl;
    return 0;
}
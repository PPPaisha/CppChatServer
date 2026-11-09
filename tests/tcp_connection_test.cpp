//------------------------------------------------------
// 文件名:tcp_connection_test.cpp
// 创建日期:2026-11-9
// 创建者:pppaisha
// 文件描述:tcp连接类测试文件
//------------------------------------------------------
#include "net/tcp_connection.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <cassert>
#include <iostream>
#include <thread>
#include <unordered_map>
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

    int ret = ::getsockname(server.getFd(),reinterpret_cast<sockaddr*>(&address),&length);
    assert(ret == 0);

    int port = ntohs(address.sin_port);
    std::unordered_map<int,std::shared_ptr<TcpConnection>> connections;
    std::string received;
    loop.add(server.getFd(),EPOLLIN,[&](uint32_t)
    {
        while(auto accepted == server.accept())
        {
            auto connection = std::make_shared<TcpConnection>(loop,std::move(*accepted));
            int fd = connection->getFd();
            connection->setMessageCallback([&](const std::string& data)
        {
            received += data;
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
        int result = ::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address));
        assert(result == 0);
        const std::string message = "Hello, TcpConnection!";

        ssize_t sent = ::send(client.getFd(),message.data(),message.size(),0);
        assert(sent == static_cast<ssize_t>(message.size()));
        ::shutdown(client.getFd(),SHUT_WR);
    });
    loop.run();
    clientThread.join();
    assert(received == "Hello, TcpConnection!");
    assert(connections.empty());
    std::cout << "TcpConnection test passed!" << std::endl;
    return 0;
}
//------------------------------------------------------
// 文件名:tcp_connection_codec_test.cpp
// 创建日期:2026-11-11
// 创建者:pppaisha
// 文件描述:使用TcpConnection和LengthFieldCodec测试TCP连接的消息收发功能
//------------------------------------------------------
#include "net/tcp_connection.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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
    std::vector<std::string> received;

    loop.add(server.getFd(),EPOLLIN,[&](uint32_t)
    {
        while(auto accepted = server.accept())
        {
            auto connection = std::make_shared<TcpConnection>(loop,std::move(*accepted));
            int fd = connection->getFd();
            connection->setMessageCallback([&](const std::string& message)
            {
                received.push_back(message);
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

    std::thread clientThread([&]
    {
        Socket client;

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        int result = ::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address));
        assert(result == 0);

        std::string first = LengthFieldCodec::encode("Hello");
        std::string second = LengthFieldCodec::encode("World");
        //人为制造拆包 先发送第一个消息帧的前2字节
        ssize_t n = ::send(client.getFd(),first.data(),2,0);
        assert(n == 2);
        //再发送第一个消息帧的剩余部分和第二个消息帧
        std::string remaining = first.substr(2) + second;
        ssize_t sent = 0;
        while(sent < remaining.size())
        {
            ssize_t count = ::send(client.getFd(),remaining.data()+sent,remaining.size()-sent,0);
            assert(count > 0);
            sent += static_cast<std::size_t>(count);
        }
        ::shutdown(client.getFd(),SHUT_WR);
    });

    loop.run();
    clientThread.join();
    assert(received.size() == 2);
    assert(received[0] == "Hello");
    assert(received[1] == "World");
    assert(connections.empty());
    std::cout<<"TcpConnection and LengthFieldCodec test passed!"<<std::endl;
    return 0;
}
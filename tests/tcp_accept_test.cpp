//------------------------------------------------------
// 文件名:tcp_accept_test.cpp
// 创建日期:2026-11-9
// 创建者:pppaisha
// 文件描述:使用EventLoop和Socket类测试TCP连接的accept功能
//------------------------------------------------------
#include "net/event_loop.h"
#include "net/socket.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

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
    if(ret == -1)
    {
        throw std::runtime_error("getsockname failed");
    }

    int port = ntohs(address.sin_port);
    int acceptedCount = 0;
    loop.add(server.getFd(),EPOLLIN,[&](uint32_t events)
    {
        if(!(events & EPOLLIN))
        {
            return;
        }
        while(true)
        {
            auto client = server.accept();
            if(!client)
            {
                break;
            }
            ++acceptedCount;
            std::cout<<"Accepted client fd:"<<client->getFd()<<std::endl;
        }
        loop.stop();
    });

    std::thread worker([&]
    {
        loop.run();
    });

    while(!loop.isRunning())
    {
        std::this_thread::yield();
    }
    Socket client;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    ret = ::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address));
    if(ret == -1)
    {
        loop.stop();
        worker.join();
        throw std::runtime_error("connect failed");
    }
    worker.join();
    assert(acceptedCount == 1);
    std::cout<<"TCP accept test passed!"<<std::endl;
    return 0;
}
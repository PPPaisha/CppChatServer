//------------------------------------------------------
// 文件名:socket_accept_test.cpp
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:用于测试接受连接的套接字功能
//------------------------------------------------------
#include "net/socket.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <cassert>
#include <arpa/inet.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

int main()
{
    Socket server;
    server.setReuseAddr(true);
    server.setNonBlocking();
    server.bind(0);
    server.listen();

    sockaddr_in address{};
    socklen_t length = sizeof(address);

    int ret = ::getsockname(server.getFd(),reinterpret_cast<sockaddr*>(&address), &length);
    assert(ret == 0);

    auto empty = server.accept();
    assert(!empty.has_value());
    
    Socket client;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    ret = ::connect(client.getFd(), reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if(ret == -1)
    {
        throw std::runtime_error("connect failed");
    }

    std::optional<Socket> accepted;
    for(int i=0; i<1000 && !accepted; ++i)
    {
        accepted = server.accept();
        if(!accepted)
        {
            ::usleep(1000);
        }
    }
    assert(accepted.has_value());
    assert(accepted -> valid());
    std::cout<<"Socket accept test passed!"<<std::endl;
    return 0;
}
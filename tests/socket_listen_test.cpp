//------------------------------------------------------
// 文件名:socket_listen_test.cpp
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:用于测试监听套接字的功能
//------------------------------------------------------
#include "net/socket.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <cassert>
#include <iostream>

int main()
{
    Socket server;
    server.setReuseAddr(true);
    server.setNonBlocking();

    server.bind(0);
    server.listen(128);
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int ret = ::getsockname(server.getFd(),reinterpret_cast<sockaddr*>(&addr), &len);
    assert(ret == 0);
    assert(ntohs(addr.sin_port) != 0);
    int accepting = 0;
    socklen_t optlen = sizeof(accepting);
    ret = ::getsockopt(server.getFd(),SOL_SOCKET,SO_ACCEPTCONN,&accepting,&optlen);
    assert(ret == 0);
    assert(accepting == 1);
    std::cout<<"Socket listen test passed!"<<std::endl;
    return 0;
}
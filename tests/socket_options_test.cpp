//------------------------------------------------------
// 文件名:socket_options_test.cpp
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:用于测试非阻塞套接字和地址重用选项的功能
//------------------------------------------------------
#include "net/socket.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <cassert>
#include <iostream>

int main()
{
    Socket sock;
    assert(sock,valid());
    sock.serNonBlocking();
    int flags = ::fcntl(sock.getFd(), F_GETFL, 0);
    assert(flags != -1);
    assert((flag & O_NONBLOCK) != 0);
    sock.setReuseAddr(true);

    int opt = 0;
    socklen_t len = sizeof(opt);
    int ret = ::getsockopt(sock.getFd(), SOL_SOCKET, SO_REUSEADDR, &opt, &len);
    assert(ret == 0);
    assert(opt == 1);
    std::cout<<"Socket options test passed!"<<std::endl;
    return 0;
}
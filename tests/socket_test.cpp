//------------------------------------------------------
// 文件名:socket_test.cpp
// 创建日期:2026-11-4
// 创建者:pppaisha
// 文件描述:用于测试socket类的功能
//------------------------------------------------------
#include "net/socket.h"

#include <cassert>
#include <utility>
#include <iostream>

int main()
{
    Socket s1;
    assert(s1.valid());
    int originalFd = s1.getFd();
    Socket s2(std::move(s1));

    assert(!s1.valid());
    assert(s2.valid());
    assert(s2.getFd() == originalFd);

    std::cout<<"Socket move test passed!"<<std::endl;
    return 0;
}
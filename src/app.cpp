//------------------------------------------------------
// 文件名:app.cpp
// 创建日期:2026-11-3
// 创建者:pppaisha
// 文件描述:应用程序主类实现文件
//------------------------------------------------------
#include "app.h"

#include <iostream>

void App::run()
{
    std::cout<< "Starting CppChatServer..." << std::endl;
}

void App::stop()
{
    std::cout<< "Stopping CppChatServer..." << std::endl;
}
//------------------------------------------------------
// 文件名:signal_handler.h
// 创建日期:2026-11-26
// 创建者:pppaisha
// 文件描述:信号处理程序头文件
//------------------------------------------------------
#pragma once

#include <functional>
#include <cstdint>

class EventLoop;
class SignalHandler
{
public:
    using SignalCallback = std::function<void(int)>;
    
    static void blockSignals();//屏蔽指定信号

    SignalHandler(EventLoop& loop, SignalCallback callback);
    ~SignalHandler();//注销事件并释放文件描述符
    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

private:
    void handleRead(uint32_t events);//读取信号并触发回调
private:
    EventLoop& m_loop; //保存所属EventLoop
    SignalCallback m_callback; //保存收到信号后的处理逻辑
    int m_signalFd = -1; //保存signalfd文件描述符
};
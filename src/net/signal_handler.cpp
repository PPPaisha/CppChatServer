//------------------------------------------------------
// 文件名:server_config.cpp
// 创建日期:2026-11-26
// 创建者:pppaisha
// 文件描述:服务器配置实现文件
//------------------------------------------------------
#include "net/signal_handler.h"
#include "net/event_loop.h"

#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>
#include <stdexcept>
#include <utility>

namespace
{
    //sigset_t 表示一个信号集合
    sigset_t createSignalMask()
    {
        sigset_t mask;
        ::sigemptyset(&mask);//清空集合
        ::sigaddset(&mask, SIGINT);
        ::sigaddset(&mask, SIGTERM);
        return mask;
    }
}

void SignalHandler::blockSignals()
{
    sigset_t mask = createSignalMask();
    int result = ::pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    if(result != 0)
    {
        throw std::system_error(result, std::generic_category(), "pthread_sigmask failed");
    }
}

SignalHandler::SignalHandler(EventLoop& loop, SignalCallback callback)
    :m_loop(loop),m_callback(std::move(callback))
{
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("SignalHandler must run in loop thread");
    }
    sigset_t mask = createSignalMask();
    m_signalFd = ::signalfd(-1,&mask,SFD_NONBLOCK | SFD_CLOEXEC);
    if(m_signalFd < 0)
    {
        throw std::system_error(errno, std::generic_category(),"signalfd failed");
    }

    try
    {
        m_loop.add(m_signalFd,EPOLLIN,[this](uint32_t events)
        {
            handleRead(events);
        });
    }
    catch(...)
    {
        ::close(m_signalFd);
        m_signalFd = -1;
        throw;
    }
}

SignalHandler::~SignalHandler()
{
    if(m_signalFd >=0 )
    {
        m_loop.remove(m_signalFd);
        ::close(m_signalFd);
        m_signalFd = -1;
    }
}

void SignalHandler::handleRead(uint32_t events)
{
    if(!(events & EPOLLIN))
    {return;}
    signalfd_siginfo info{};
    while(true)
    {
        ssize_t n = ::read(m_signalFd,&info,sizeof(info));
        if(n == sizeof(info))
        {
            if(m_callback)
            {
                m_callback(static_cast<int>(info.ssi_signo));
            }
        }
        else if(n < 0 && errno == EINTR)
        {
            continue;
        }
        else if(n < 0 && errno == EAGAIN)
        {
            break;
        }
        else
        {
            throw std::system_error(n < 0 ? errno:EIO, std::generic_category(),"signalfd read failed");
        }
    }

}
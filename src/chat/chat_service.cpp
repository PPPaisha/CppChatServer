//------------------------------------------------------
// 文件名:chat_service.cpp
// 创建日期:2026-11-18
// 创建者:pppaisha
// 文件描述:客户端聊天服务实现文件
//------------------------------------------------------
#include "chat/chat_service.h"

#include "net/event_loop.h"
#include "net/tcp_connection.h"

#include <stdexcept>
#include <utility>
#include <vector>

ChatService::ChatService(EventLoop& loop)
    :m_loop(loop)
{
}

void ChatService::ensureLoopThread() const
{
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("ChatService must run in EventLoop thread");
    }
}

bool ChatService::registerUser(const std::string& userId, const std::shared_ptr<TcpConnection>& connection)
{
    ensureLoopThread();
    if(userId.empty() || !connection ||connection->getFd() < 0)
    {
        return false;
    }
    int fd = connection->getFd();
    if(m_sessions.find(userId) != m_sessions.end() || m_fdToUser.find(fd) != m_fdToUser.end())
    {
        return false;
    }
    UserSession session{userId,connection};
    auto [it, inserted] = m_sessions.emplace(userId, std::move(session));
    if(!inserted)
    {
        return false;
    }
    try
    {
        auto result = m_fdToUser.emplace(fd,userId);
        if(!result.second)
        {
            m_sessions.erase(it);
            return false;
        }
    }
    catch(...)
    {
        m_sessions.erase(it);
        throw;
    }
    return true;
}

bool ChatService::isRegisteredConnection(int fd) const
{
    ensureLoopThread();
    return m_fdToUser.find(fd) != m_fdToUser.end();
}

bool ChatService::removeUser(const std::string& userId)
{
    ensureLoopThread();
    auto it = m_sessions.find(userId);
    if(it == m_sessions.end())
    {
        return false;
    }
    const int fd = it->second.fd;
    auto fdIt = m_fdToUser.find(fd);
    if(fdIt != m_fdToUser.end() && fdIt->second == userId)
    {
        m_fdToUser.erase(fdIt);
    }
    m_sessions.erase(it);
    return true;
}

void ChatService::removeConnection(int fd)
{
    ensureLoopThread();
    auto it = m_fdToUser.find(fd);
    if(it == m_fdToUser.end())
    {
        return;
    }
    const std::string userId = it->second;
    removeUser(userId);
}

bool ChatService::hasUser(const std::string& userId) const
{
    ensureLoopThread();
    return m_sessions.find(userId) != m_sessions.end();
}

std::size_t ChatService::onlineCount() const 
{
    ensureLoopThread();
    return m_sessions.size();
}

bool ChatService::sendToUser(const std::string& userId, const std::string& message)
{
    ensureLoopThread();
    auto it = m_sessions.find(userId);
    if(it == m_sessions.end())
    {return false;}
    auto connection = it->second.connection.lock();
    if(!connection)
    {
        removeUser(userId);
        return false;
    }
    return connection->trySendMessage(message);
}

std::size_t ChatService::broadcast(const std::string& senderId, const std::string& message)
{
    ensureLoopThread();
    std::vector<std::shared_ptr<TcpConnection>> targets;
    for(const auto& [userId,session] : m_sessions)
    {
        if(userId == senderId)
        {
            continue;
        }
        if(auto connection = session.connection.lock())
        {
            targets.push_back(std::move(connection));
        }
    }
    std::size_t acceptedCount = 0;
    for(const auto& connection: targets)
    {
        if(connection->trySendMessage(message))
        {
            ++acceptedCount;
        }
    }
    return acceptedCount;
}

void ChatService::handleMessage(const std::shared_ptr<TcpConnection>& connection, const std::string& message)
{
    ensureLoopThread();
    if(!connection)
    {
        return;
    }
    const int fd = connection->getFd();
    //LOGIN用户名
    if(message.rfind("LOGIN ",0) == 0)
    {
        std::string userId = message.substr(6);
        bool valid = !userId.empty() && userId.find(" ") == std::string::npos;
        bool success = valid && registerUser(userId,connection);
        connection->trySendMessage(success ? "LOGIN_OK" : "LOGIN_FAIL");
        return;
    }
    //其他命令必须先登录
    auto senderIt = m_fdToUser.find(fd);
    if(senderIt == m_fdToUser.end())
    {
        connection->trySendMessage("ERROR Not logged in");
        return;
    }
    //复制用户名 避免发送过程中会话删除导致引用失效
    const std::string senderId = senderIt->second;
    //SEND 接收者 消息内容
    if(message.rfind("SEND ",0) == 0)
    {
        std::size_t separator = message.find(' ',5);
        if(separator == std::string::npos || separator == 5 || separator+1 >= message.size())
        {
            connection->trySendMessage("ERROR: Invalid SEND format");
            return;
        }
        std::string targetId = message.substr(5,separator-5);
        std::string content = message.substr(separator+1);
        bool success = sendToUser(targetId,"FROM "+ senderId + " "+content);
        connection->trySendMessage(success ? "SEND_OK" : "SEND_FAILED");
        return;
    }
    //BROADCAST 消息内容
    if(message.rfind("BROADCAST",0) == 0)
    {
        std::string content = message.substr(10);
        if(content.empty())
        {
            connection->trySendMessage("ERROR: Invalid BROADCAST format");
            return;
        }
        broadcast(senderId,"BROADCAST_FROM " + senderId + " " + content);
        connection->trySendMessage("BROADCAST_OK");
        return;
    }
    connection->trySendMessage("ERROR Invalid command");
}
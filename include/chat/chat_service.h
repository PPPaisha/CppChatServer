//------------------------------------------------------
// 文件名:chat_service.h
// 创建日期:2026-11-18
// 创建者:pppaisha
// 文件描述:客户端聊天服务头文件
//------------------------------------------------------
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

class EventLoop;
class TcpConnection;

struct UserSession
{
    std::string userId;
    int fd = -1;
    std::weak_ptr<TcpConnection> connection;
};

class ChatService
{
public:
    explicit ChatService(EventLoop& loop);
    void removeConnection(int fd);
    void handleMessage(const std::shared_ptr<TcpConnection>& connection, const std::string& message);
    bool registerUser(const std::string& userId, const std::shared_ptr<TcpConnection>& connection);
    bool removeUser(const std::string& userId);
    bool hasUser(const std::string& userId) const;
    bool isRegisteredConnection(int fd) const; //用于检查一个连接是否已经登录
    bool sendToUser(const std::string& userId, const std::string& message);
    std::size_t onlineCount() const;
    std::size_t broadcast(const std::string& senderId, const std::string& message);

private:
    void ensureLoopThread() const;
    EventLoop& m_loop;
    std::unordered_map<std::string,UserSession> m_sessions;
    std::unordered_map<int,std::string> m_fdToUser;
};
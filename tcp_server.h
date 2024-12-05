#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>
#include "event_loop.h"
#include "tcp_connection.h"
#include "thread_pool.h"

class TcpServer {
  public:
    using MessageCallback = TcpConnection::MessageCallback;
    using CloseCallback = TcpConnection::CloseCallback;
    using ErrorCallback = TcpConnection::ErrorCallback;

    TcpServer(const std::string& ip, uint16_t port, size_t threadNum = 4);

    void setMessageCallback(const MessageCallback& cb) {
        messageCallback_ = cb;
    }
    void setCloseCallback(const CloseCallback& cb) {
        closeCallback_ = cb;
    }
    void setErrorCallback(const ErrorCallback& cb) {
        errorCallback_ = cb;
    }

    void start();

  private:
    void handleAccept();

    std::unique_ptr<EventLoop> acceptLoop_;  // 专门处理新连接
    std::unique_ptr<EventLoop> ioLoop_;      // 处理IO事件
    std::unique_ptr<ThreadPool> threadPool_; // 处理业务逻辑
    Socket listenSocket_;
    std::unordered_map<int, std::shared_ptr<TcpConnection>> connections_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    ErrorCallback errorCallback_;
	std::mutex mutex_;
};

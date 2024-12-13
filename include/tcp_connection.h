#pragma once
#include <memory>
#include <functional>
#include "socket.h"
#include "ring_buffer.h"
#include "message.h"
#include "event_loop.h"
#include "thread_pool.h"

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
  public:
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                            const MessageHeader&,
                            const std::vector<char>&)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using ErrorCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                          const std::string&)>;

    TcpConnection(Socket socket, EventLoop* loop, ThreadPool* threadPool);

    ~TcpConnection();

    void setMessageCallback(const MessageCallback& cb) {
        messageCallback_ = cb;
    }
    void setCloseCallback(const CloseCallback& cb) {
        closeCallback_ = cb;
    }
    void setErrorCallback(const ErrorCallback& cb) {
        errorCallback_ = cb;
    }

    // 发送数据的方法
    void send(const MessageHeader& header, const std::vector<char>& body);

    void sendMessage(uint16_t type, const std::string& data);

    void start();

    int fd() const {
        return socket_.fd();
    }

    void close();

  private:
    void handleRead();

    void handleWrite();

    void handleMessage(const MessageHeader& header, const std::vector<char>& body);

    void processMessages();

    Socket socket_;
    EventLoop* loop_;
    ThreadPool* threadPool_;
    RingBuffer readBuffer_;
    RingBuffer writeBuffer_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    ErrorCallback errorCallback_;
    bool closed_;
};

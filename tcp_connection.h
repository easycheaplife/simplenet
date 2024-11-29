#pragma once
#include "socket.h"
#include "ring_buffer.h"
#include "message.h"
#include "event_loop.h"
#include "thread_pool.h"
#include <memory>
#include <functional>

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
	using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
			const MessageHeader&,
			const std::vector<char>&)>;
	using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
	using ErrorCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
			const std::string&)>;

    TcpConnection(Socket socket, EventLoop* loop, ThreadPool* threadPool)
        : socket_(std::move(socket))
        , loop_(loop)
        , threadPool_(threadPool)
        , readBuffer_(64 * 1024)
        , writeBuffer_(64 * 1024) 
		, closed_(false) {
		LOG_INFO("TcpConnection created with fd: {}", socket_.fd());
    }

	~TcpConnection() {
		LOG_INFO("TcpConnection destroyed with fd: {}", socket_.fd());
	}

    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
	void setErrorCallback(const ErrorCallback& cb) { errorCallback_ = cb; }

    // 发送数据的方法
    void send(const MessageHeader& header, const std::vector<char>& body) {
        writeBuffer_.write(reinterpret_cast<const char*>(&header), sizeof(header));
        writeBuffer_.write(body.data(), body.size());
        
        loop_->addEvent(socket_.fd(), Reactor::EventType::WRITE,
            std::bind(&TcpConnection::handleWrite, this));
    }

	void sendMessage(uint16_t type, const std::string& data) {
		MessageHeader header;
		header.magic = 0x12345678;
		header.version = 1;
		header.type = type;
		header.length = data.size();

		writeBuffer_.write(reinterpret_cast<const char*>(&header), sizeof(header));
		writeBuffer_.write(data.data(), data.size());

		if (!loop_->addEvent(socket_.fd(), Reactor::EventType::WRITE,
					std::bind(&TcpConnection::handleWrite, shared_from_this()))) {
			LOG_ERROR("Failed to add write event");
			close();
			return;
		}
	}

	void start() {
		if (!socket_.isValid()) {
			LOG_ERROR("Invalid socket in start()");
			return;
		}

		LOG_INFO("Starting connection for fd: {}", socket_.fd());
		if (!loop_->addEvent(socket_.fd(), Reactor::EventType::READ,
					std::bind(&TcpConnection::handleRead, this))) {
			LOG_ERROR("Failed to add read event for fd: {}", socket_.fd());
			close();
			return;
		}
	}

    int fd() const { return socket_.fd(); }


private:
	void handleRead() {
		char buf[4096];
		while (true) {  // 使用循环读取所有数据
			ssize_t n = socket_.read(buf, sizeof(buf));
			if (n > 0) {
				readBuffer_.write(buf, n);
			} else if (n == 0) {
				LOG_INFO("Connection closed by peer, fd: {}", socket_.fd());
				close();
				return;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					break;  // 没有更多数据可读
				}
				LOG_ERROR("Read error for fd {}: {}", socket_.fd(), strerror(errno));
				close();
				return;
			}
		}
		processMessages();
    }

    void handleWrite() {
		while (writeBuffer_.readableBytes() > 0) {
			ssize_t n = socket_.write(writeBuffer_.peek(), writeBuffer_.readableBytes());
			if (n > 0) {
				writeBuffer_.retrieve(n);
			} else if (n == 0) {
				break;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					break;
				}
				LOG_ERROR("Write error for fd {}: {}", socket_.fd(), strerror(errno));
				close();
				return;
			}
		}

		if (writeBuffer_.readableBytes() == 0) {
			loop_->removeEvent(socket_.fd(), Reactor::EventType::WRITE);
		}
    }

    void handleMessage(const MessageHeader& header, const std::vector<char>& body) {
        if (messageCallback_) {
			messageCallback_(shared_from_this(), header, body);
        }
    }

	void close() {
		if (closed_) {
			return;
		}
		closed_ = true;

		LOG_INFO("Closing connection for fd: {}", socket_.fd());

		// 先移除事件，再关闭socket
		if (socket_.isValid()) {
			loop_->removeEvent(socket_.fd(), Reactor::EventType::READ);
			loop_->removeEvent(socket_.fd(), Reactor::EventType::WRITE);
		}

		int fd = socket_.fd();
		socket_.close();

		if (closeCallback_) {
			closeCallback_(shared_from_this());
		}

		LOG_INFO("Connection closed, fd: {}", fd);
    }

    void processMessages() {
		while (readBuffer_.readableBytes() >= Message::HEADER_SIZE) {
			MessageHeader header;
			if (!readBuffer_.peek(&header, Message::HEADER_SIZE)) {
				LOG_ERROR("Failed to peek message header");
				close();
				return;
			}

			if (header.magic != 0x12345678) {
				LOG_ERROR("Invalid message magic: {:x}", header.magic);
				if (errorCallback_) {
					errorCallback_(shared_from_this(), "Invalid message magic");
				}
				close();
				return;
			}

			if (readBuffer_.readableBytes() < Message::HEADER_SIZE + header.length) {
				break;
			}

			readBuffer_.retrieve(Message::HEADER_SIZE);
			std::vector<char> body(header.length);
			if (readBuffer_.read(body.data(), header.length) != header.length) {
				LOG_ERROR("Failed to read message body");
				if (errorCallback_) {
					errorCallback_(shared_from_this(), "Failed to read message body");
				}
				close();
				return;
			}

			LOG_INFO("Received message: type={}, length={}", header.type, header.length);

			// 调用消息处理回调
			if (messageCallback_) {
				threadPool_->submit([this, header, body = std::move(body)]() {
						handleMessage(header, body);
						});
			}
		}
    }

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

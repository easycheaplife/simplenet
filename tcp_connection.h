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
		if (closed_) {
			LOG_ERROR("Attempting to send on closed connection");
			return;
		}

		MessageHeader header;
		header.magic = 0x12345678;
		header.version = 1;
		header.type = type;
		header.length = data.size();

		size_t totalSize = sizeof(header) + data.size();
		if (writeBuffer_.writableBytes() < totalSize) {
			LOG_ERROR("Write buffer overflow, needed: {}, available: {}",
					totalSize, writeBuffer_.writableBytes());
			close();
			return;
		}

		LOG_INFO("Sending message: type={}, length={}", type, data.size());

		if (!writeBuffer_.write(reinterpret_cast<const char*>(&header), sizeof(header))) {
			LOG_ERROR("Failed to write header to buffer");
			close();
			return;
		}

		if (!writeBuffer_.write(data.data(), data.size())) {
			LOG_ERROR("Failed to write data to buffer");
			close();
			return;
		}

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
		const size_t CHUNK_SIZE = 4096;
		char buf[CHUNK_SIZE];
		size_t totalRead = 0;

		LOG_INFO("Handling read event for fd: {}", socket_.fd());

		while (true) {
			ssize_t n = socket_.read(buf, CHUNK_SIZE);
			if (n > 0) {
				LOG_INFO("Read {} bytes from fd: {}", n, socket_.fd());
				totalRead += n;
				readBuffer_.write(buf, n);
			} else if (n == 0) {
				LOG_INFO("Connection closed by peer after reading {} bytes, fd: {}", 
						totalRead, socket_.fd());
				close();
				return;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					LOG_DEBUG("No more data to read after {} bytes, fd: {}", 
							totalRead, socket_.fd());
					break;
				}
				LOG_ERROR("Read error for fd {}: {}", socket_.fd(), strerror(errno));
				close();
				return;
			}
		}

		if (totalRead > 0) {
			LOG_INFO("Processing {} bytes of data from fd: {}", 
					totalRead, socket_.fd());
			processMessages();
		}
	}
	void handleWrite() {
		size_t totalWritten = 0;

		while (writeBuffer_.readableBytes() > 0) {
			ssize_t n = socket_.write(writeBuffer_.peek(), writeBuffer_.readableBytes());
			if (n > 0) {
				totalWritten += n;
				writeBuffer_.retrieve(n);
			} else if (n == 0) {
				LOG_INFO("Connection closed during write");
				close();
				return;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					LOG_DEBUG("Write would block after {} bytes", totalWritten);
					break;
				}
				LOG_ERROR("Write error for fd {}: {}", socket_.fd(), strerror(errno));
				close();
				return;
			}
		}

		if (writeBuffer_.readableBytes() == 0) {
			LOG_DEBUG("Write buffer empty, removing write event");
			loop_->removeEvent(socket_.fd(), Reactor::EventType::WRITE);
		}

		if (totalWritten > 0) {
			LOG_INFO("Successfully wrote {} bytes", totalWritten);
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
				LOG_ERROR("Invalid message magic: {:x}, closing connection", header.magic);
				close();
				return;
			}

			size_t totalMessageSize = Message::HEADER_SIZE + header.length;
			if (header.length > 10 * 1024 * 1024) { // 10MB limit
				LOG_ERROR("Message too large: {} bytes", header.length);
				close();
				return;
			}

			if (readBuffer_.readableBytes() < totalMessageSize) {
				LOG_DEBUG("Incomplete message, waiting for more data. "
						"Have: {}, need: {}",
						readBuffer_.readableBytes(), totalMessageSize);
				break;
			}

			readBuffer_.retrieve(Message::HEADER_SIZE);
			std::vector<char> body(header.length);
			if (readBuffer_.read(body.data(), header.length) != header.length) {
				LOG_ERROR("Failed to read message body");
				close();
				return;
			}

			LOG_INFO("Successfully processed message: type={}, length={}",
					header.type, header.length);

			if (messageCallback_) {
				threadPool_->submit([this, header, body = std::move(body)]() {
						try {
						handleMessage(header, body);
						} catch (const std::exception& e) {
						LOG_ERROR("Error in message handler: {}", e.what());
						}
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

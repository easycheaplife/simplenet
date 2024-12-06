#include "tcp_server.h"

TcpServer::TcpServer(const std::string& ip, uint16_t port, size_t threadNum)
	: acceptLoop_(new EventLoop())
	, ioLoop_(new EventLoop())
	  , threadPool_(new ThreadPool(threadNum))
	  , listenSocket_() {

		  // 创建并初始化监听socket
		  if (!listenSocket_.create()) {
			  throw std::runtime_error("Create listen socket failed");
		  }

		  // 设置地址重用
		  listenSocket_.setReuseAddr();

		  // 绑定地址
		  if (!listenSocket_.bind(ip, port)) {
			  throw std::runtime_error("Bind failed");
		  }

		  // 开始监听
		  if (!listenSocket_.listen()) {
			  throw std::runtime_error("Listen failed");
		  }

		  LOG_INFO("Server listening on {}:{}", ip, port);
	  }

void TcpServer::start() {
	// 将监听socket的读事件加入acceptLoop
	if (!acceptLoop_->addEvent(listenSocket_.fd(), Reactor::EventType::READ,
				std::bind(&TcpServer::handleAccept, this))) {
		throw std::runtime_error("Add accept event failed");
	}

	// 启动事件循环
	acceptLoop_->start();  // Accept线程
	ioLoop_->start();      // IO线程
}

void TcpServer::handleAccept() {
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	int clientFd = ::accept(listenSocket_.fd(), (struct sockaddr*)&addr, &addrlen);

	if (clientFd < 0) {
		LOG_ERROR("Accept failed: {}", strerror(errno));
		return;
	}

	LOG_INFO("New connection from {}:{} with fd: {}",
			inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), clientFd);

	// 设置新socket的属性
	int flags = fcntl(clientFd, F_GETFL, 0);
	fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

	// 创建新的Socket对象
	Socket clientSocket(clientFd);

	std::lock_guard<std::mutex> lock(mutex_);
	// 创建连接对象
	auto conn = std::make_shared<TcpConnection>(
			std::move(clientSocket),
			ioLoop_.get(),
			threadPool_.get()
			);

	if (messageCallback_) conn->setMessageCallback(messageCallback_);
	if (errorCallback_) conn->setErrorCallback(errorCallback_);

	// 设置关闭回调
	auto fd = conn->fd();
	conn->setCloseCallback([this, fd](const std::shared_ptr<TcpConnection>& conn) {
			LOG_INFO("Connection closed, removing fd: {}", fd);
			connections_.erase(fd);
			if (closeCallback_) closeCallback_(conn);
			});

	// 保存连接
	connections_[fd] = conn;

	// 启动连接
	conn->start();
}


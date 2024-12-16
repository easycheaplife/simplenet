#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"

class Socket {
  public:
    Socket() : fd_(-1) {}

    Socket(const std::string& ip, uint16_t port) : fd_(-1) {
        create();
        bind(ip, port);
    }

    explicit Socket(int fd) : fd_(fd) {
        setNonBlocking();
        setTcpNoDelay();
    }

    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;  // 防止其他对象关闭文件描述符
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~Socket() {
        close();
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool create() {
        close();
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            LOG_ERROR("Create socket failed: {}", strerror(errno));
            return false;
        }
        setNonBlocking();
        setTcpNoDelay();
        return true;
    }

    bool bind(const std::string& ip, uint16_t port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (::bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_ERROR("Bind failed: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool listen(int backlog = SOMAXCONN) {
        if (::listen(fd_, backlog) < 0) {
            LOG_ERROR("Listen failed: {}", strerror(errno));
            return false;
        }
        LOG_ERROR("Listen ok: backlog {}", SOMAXCONN);
        return true;
    }

    Socket accept() {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int clientfd = ::accept(fd_, (struct sockaddr*)&addr, &len);
        if (clientfd < 0) {
            LOG_ERROR("Accept failed: {}", strerror(errno));
            return Socket(-1);
        }
        return Socket(clientfd);
    }

    ssize_t read(void* buf, size_t count) {
        return ::read(fd_, buf, count);
    }

    ssize_t write(const void* buf, size_t count) {
        return ::write(fd_, buf, count);
    }

    bool setNonBlocking() {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0) {
            LOG_ERROR("Get flags failed: {}", strerror(errno));
            return false;
        }
        if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            LOG_ERROR("Set non-blocking failed: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool setBlocking() {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0) {
            LOG_ERROR("Get flags failed: {}", strerror(errno));
            return false;
        }
        flags &= ~O_NONBLOCK;
        if (fcntl(fd_, F_SETFL, flags) < 0) {
            LOG_ERROR("Set blocking failed: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool setTcpNoDelay() {
        int optval = 1;
        if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
                       &optval, sizeof(optval)) < 0) {
            LOG_ERROR("Set TCP_NODELAY failed: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool setReuseAddr() {
        int optval = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof(optval)) < 0) {
            LOG_ERROR("Set SO_REUSEADDR failed: {}", strerror(errno));
            return false;
        }
        return true;
    }

    int fd() const {
        return fd_;
    }
    bool isValid() const {
        return fd_ >= 0;
    }

  private:
    int fd_;
};

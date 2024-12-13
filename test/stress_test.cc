#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <iomanip>
#include <mutex>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include "message.h"


#if 1
#ifndef FUN_RECEIVE_RESPONSE
#define FUN_RECEIVE_RESPONSE
#endif // FUN_RECEIVE_RESPONSE
#endif

// Socket类
class Socket {
  public:
    Socket() : fd_(-1) {}

    explicit Socket(int fd) : fd_(fd) {
        if (fd_ >= 0) {
            setNonBlocking();
            setTcpNoDelay();
        }
    }

    ~Socket() {
        close();
    }

    bool create() {
        close();
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ >= 0) {
            setNonBlocking();
            setTcpNoDelay();
        }
        return fd_ >= 0;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool setNonBlocking() {
        int flags = fcntl(fd_, F_GETFL, 0);
        return fcntl(fd_, F_SETFL, flags | O_NONBLOCK) >= 0;
    }

    bool setTcpNoDelay() {
        int optval = 1;
        return setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) >= 0;
    }

    ssize_t read(void* buf, size_t len) {
        return ::read(fd_, buf, len);
    }

    ssize_t write(const void* buf, size_t len) {
        return ::write(fd_, buf, len);
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

// 统计信息类
struct Statistics {
    std::atomic<uint64_t> totalRequests{0};
    std::atomic<uint64_t> successRequests{0};
    std::atomic<uint64_t> failedRequests{0};
    std::atomic<uint64_t> totalLatency{0};
    std::atomic<uint64_t> maxLatency{0};
    std::atomic<uint64_t> minLatency{std::numeric_limits<uint64_t>::max()};

    void updateLatency(uint64_t latency) {
        totalLatency += latency;
        uint64_t current = maxLatency.load();
        while (latency > current) {
            if (maxLatency.compare_exchange_weak(current, latency)) break;
        }
        current = minLatency.load();
        while (latency < current) {
            if (minLatency.compare_exchange_weak(current, latency)) break;
        }
    }

    void print(std::chrono::seconds duration) {
        uint64_t total = totalRequests.load();
        uint64_t success = successRequests.load();
        uint64_t failed = failedRequests.load();

        std::cout << "\nTest Results:" << std::endl;
        std::cout << "Duration: " << duration.count() << " seconds" << std::endl;
        std::cout << "Total Requests: " << total << std::endl;
        std::cout << "Successful Requests: " << success << std::endl;
        std::cout << "Failed Requests: " << failed << std::endl;

        if (duration.count() > 0) {
            std::cout << "Requests/Second: " << total / duration.count() << std::endl;
        }

        if (success > 0) {
            uint64_t avgLatency = totalLatency.load() / success;
            std::cout << "Average Latency: " << avgLatency << " us" << std::endl;
            std::cout << "Min Latency: " << minLatency.load() << " us" << std::endl;
            std::cout << "Max Latency: " << maxLatency.load() << " us" << std::endl;
        }
    }
};

// 客户端类
class StressTestClient {
  public:
    StressTestClient(const std::string& ip, uint16_t port, int timeoutMs = 5000)
        : ip_(ip), port_(port), socket_(), connected_(false) {
        if (!socket_.create()) {
            throw std::runtime_error("Create socket failed: " + std::string(strerror(errno)));
        }

        // 设置 keep-alive
        int optval = 1;
        if (setsockopt(socket_.fd(), SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
            std::cerr << "Warning: Failed to set SO_KEEPALIVE" << std::endl;
        }

        // 设置 TCP_NODELAY
        if (setsockopt(socket_.fd(), IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
            std::cerr << "Warning: Failed to set TCP_NODELAY" << std::endl;
        }

        // 设置发送和接收缓冲区大小
        int bufSize = 64 * 1024;  // 64KB
        if (setsockopt(socket_.fd(), SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize)) < 0) {
            std::cerr << "Warning: Failed to set SO_RCVBUF" << std::endl;
        }
        if (setsockopt(socket_.fd(), SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) < 0) {
            std::cerr << "Warning: Failed to set SO_SNDBUF" << std::endl;
        }

        if (!connectWithTimeout(timeoutMs)) {
            throw std::runtime_error("Connect failed: " + std::string(strerror(errno)));
        }
        connected_ = true;
    }


    ~StressTestClient() {
        if (connected_) {
            socket_.close();
        }
    }

    bool connectWithTimeout(int timeoutMs) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = inet_addr(ip_.c_str());

        int ret = ::connect(socket_.fd(), (struct sockaddr*)&addr, sizeof(addr));
        if (ret == 0) {
            return true;
        }

        if (errno != EINPROGRESS) {
            return false;
        }

        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(socket_.fd(), &write_fds);

        struct timeval timeout;
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;

        ret = select(socket_.fd() + 1, nullptr, &write_fds, nullptr, &timeout);
        if (ret <= 0) {
            return false;
        }

        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_.fd(), SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
            return false;
        }

        return true;
    }

    bool sendMessage(uint16_t type, const std::string& data) {
        try {
            MessageHeader header;
            header.magic = MAGIC_NUMBER;
            header.version = 1;
            header.type = type;
            header.length = data.size();

            // 先发送头部
            std::cout << "Sending header..." << std::endl;
            if (!sendAll(&header, sizeof(header))) {
                std::cerr << "Failed to send header" << std::endl;
                return false;
            }

            // 等待一小段时间确保头部发送完成
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // 再发送数据
            std::cout << "Sending data..." << std::endl;
            if (!sendAll(data.data(), data.size())) {
                std::cerr << "Failed to send data" << std::endl;
                return false;
            }

            std::cout << "Message sent successfully" << std::endl;

#ifndef FUN_RECEIVE_RESPONSE
            // 等待响应
            MessageHeader respHeader;
            if (!receiveAll(&respHeader, sizeof(respHeader))) {
                std::cerr << "Failed to receive response header" << std::endl;
                return false;
            }

            if (respHeader.magic != MAGIC_NUMBER) {
                std::cerr << "Invalid response magic number" << std::endl;
                return false;
            }

            std::vector<char> respData(respHeader.length);
            if (!receiveAll(respData.data(), respHeader.length)) {
                std::cerr << "Failed to receive response data" << std::endl;
                return false;
            }

            std::cout << "Response received successfully" << std::endl;
#endif // FUN_RECEIVE_RESPONSE
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error in sendMessage: " << e.what() << std::endl;
            return false;
        }
    }

    bool receiveResponse() {
#ifndef FUN_RECEIVE_RESPONSE
        return true;
#else
        for (int retry = 0; retry < 3; ++retry) {
            try {
                std::cout << "Waiting for response..." << std::endl;

                MessageHeader header;
                std::cout << "Attempting to receive header (" << sizeof(header) << " bytes)..." << std::endl;

                // 先尝试接收一个字节，确认服务器有响应
                char firstByte;
                if (!receiveAll(&firstByte, 1, 1000)) {
                    std::cerr << "No initial response from server, retry " << retry << std::endl;
                    reconnect();
                    continue;
                }

                // 接收剩余的header
                char* headerPtr = reinterpret_cast<char*>(&header);
                headerPtr[0] = firstByte;
                if (!receiveAll(headerPtr + 1, sizeof(header) - 1, 1000)) {
                    std::cerr << "Failed to receive complete header, retry " << retry << std::endl;
                    reconnect();
                    continue;
                }

                std::cout << "Received header: magic=" << std::hex << header.magic
                          << ", version=" << header.version
                          << ", type=" << header.type
                          << ", length=" << std::dec << header.length << std::endl;

                if (header.magic != MAGIC_NUMBER) {
                    std::cerr << "Invalid magic number: " << std::hex << header.magic << std::endl;
                    reconnect();
                    continue;
                }

                if (header.length > 10 * 1024 * 1024) {
                    std::cerr << "Invalid message length: " << header.length << std::endl;
                    reconnect();
                    continue;
                }

                std::vector<char> data(header.length);
                std::cout << "Receiving data (" << header.length << " bytes)..." << std::endl;

                if (!receiveAll(data.data(), header.length, 1000)) {
                    std::cerr << "Failed to receive data, retry " << retry << std::endl;
                    reconnect();
                    continue;
                }

                std::cout << "Response received successfully" << std::endl;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Receive error: " << e.what() << ", retry " << retry << std::endl;
                reconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        return false;
#endif // FUN_RECEIVE_RESPONSE 
    }

  private:
    void reconnect() {
        socket_.close();
        socket_.create();

        for (int retry = 0; retry < 3; ++retry) {
            if (connectWithTimeout(5000)) {
                connected_ = true;
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        connected_ = false;
    }

    bool receiveAll(void* data, size_t len, int timeoutMs = 5000) {
        char* ptr = static_cast<char*>(data);
        size_t remaining = len;
        size_t totalReceived = 0;

        auto startTime = std::chrono::steady_clock::now();

        while (remaining > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - startTime).count();

            if (elapsed > timeoutMs) {
                std::cerr << "Receive timeout after " << elapsed << "ms, "
                          << "received: " << totalReceived << " bytes, "
                          << "remaining: " << remaining << "/" << len << " bytes" << std::endl;
                return false;
            }

            ssize_t n = socket_.read(ptr, remaining);
            if (n > 0) {
                ptr += n;
                remaining -= n;
                totalReceived += n;
                std::cout << "Received " << n << " bytes, total: " << totalReceived
                          << "/" << len << std::endl;
            } else if (n == 0) {
                std::cerr << "Connection closed by peer during receive" << std::endl;
                return false;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fd_set read_fds;
                    FD_ZERO(&read_fds);
                    FD_SET(socket_.fd(), &read_fds);

                    struct timeval timeout;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 100000;  // 100ms

                    int ret = select(socket_.fd() + 1, &read_fds, nullptr, nullptr, &timeout);
                    if (ret < 0) {
                        std::cerr << "Select error in receive: " << strerror(errno) << std::endl;
                        return false;
                    }
                    continue;
                }
                std::cerr << "Read error: " << strerror(errno) << std::endl;
                return false;
            }
        }
        return true;
    }


    bool sendAll(const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t remaining = len;
        size_t totalSent = 0;

        while (remaining > 0) {
            ssize_t n = socket_.write(ptr, remaining);
            if (n > 0) {
                ptr += n;
                remaining -= n;
                totalSent += n;
                std::cout << "Sent " << n << " bytes, total: " << totalSent
                          << "/" << len << std::endl;
            } else if (n == 0) {
                std::cerr << "Connection closed by peer during send" << std::endl;
                return false;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 等待一小段时间后重试
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                std::cerr << "Write error: " << strerror(errno) << std::endl;
                return false;
            }
        }
        return true;
    }

    std::string ip_;
    uint16_t port_;
    Socket socket_;
    bool connected_;
};

// 进度条类
class ProgressBar {
  public:
    ProgressBar(size_t total, size_t width = 50)
        : total_(total), width_(width) {}

    void update(size_t current) {
        std::lock_guard<std::mutex> lock(mutex_);
        float progress = static_cast<float>(current) / total_;
        int pos = static_cast<int>(width_ * progress);

        std::cout << "\r[";
        for (int i = 0; i < width_; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1)
                  << (progress * 100.0) << "% " << std::flush;
        std::cout << std::endl;
    }

  private:
    size_t total_;
    size_t width_;
    std::mutex mutex_;
};

void setupSignalHandlers() {
    signal(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE

    struct sigaction sa;
    sa.sa_handler = [](int) { /* 优雅退出的处理逻辑 */ };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

int main(int argc, char* argv[]) {
    setupSignalHandlers();
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <ip> <port> <threads> <requests_per_thread> <message_size>"
                  << std::endl;
        return 1;
    }

    try {
        const std::string ip = argv[1];
        const uint16_t port = std::stoi(argv[2]);
        const int numThreads = std::stoi(argv[3]);
        const int requestsPerThread = std::stoi(argv[4]);
        const int messageSize = std::stoi(argv[5]);

        std::cout << "Starting stress test with:" << std::endl
                  << "Threads: " << numThreads << std::endl
                  << "Requests per thread: " << requestsPerThread << std::endl
                  << "Message size: " << messageSize << " bytes" << std::endl;

        Statistics stats;
        std::vector<std::thread> threads;
        auto startTime = std::chrono::steady_clock::now();

        ProgressBar progressBar(numThreads * requestsPerThread);

        // 生成测试消息
        std::string message(messageSize, 'A');
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis('A', 'Z');
        for (char& c : message) {
            c = dis(gen);
        }

        // 错误计数和互斥锁
        std::atomic<int> errorCount{0};
        const int MAX_ERRORS = 100;  // 增加错误容忍度
        std::mutex errorMutex;

        // 创建工作线程
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&, i]() {
                try {
                    std::unique_ptr<StressTestClient> client;

                    // 连接重试
                    for (int retry = 0; retry < 3; ++retry) {
                        try {
                            client = std::make_unique<StressTestClient>(ip, port);
                            break;
                        } catch (const std::exception& e) {
                            std::lock_guard<std::mutex> lock(errorMutex);
                            std::cerr << "Thread " << i << " connection error (retry " << retry << "): "
                                      << e.what() << std::endl;
                            if (retry == 2) {
                                errorCount++;
                                return;
                            }
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }
                    }

                    if (!client) {
                        std::lock_guard<std::mutex> lock(errorMutex);
                        std::cerr << "Thread " << i << " failed to establish connection" << std::endl;
                        return;
                    }

                    for (int j = 0; j < requestsPerThread; ++j) {
                        if (errorCount.load() >= MAX_ERRORS) {
                            std::lock_guard<std::mutex> lock(errorMutex);
                            std::cerr << "Thread " << i << " stopping due to too many errors" << std::endl;
                            return;
                        }

                        auto start = std::chrono::steady_clock::now();
                        bool success = false;

                        try {
                            success = client->sendMessage(1, message) && client->receiveResponse();
                        } catch (const std::exception& e) {
                            std::lock_guard<std::mutex> lock(errorMutex);
                            std::cerr << "Thread " << i << " request error: " << e.what() << std::endl;
                            errorCount++;
                            continue;
                        }

                        auto end = std::chrono::steady_clock::now();
                        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                                           end - start).count();

                        stats.totalRequests++;
                        if (success) {
                            stats.successRequests++;
                            stats.updateLatency(latency);
                        } else {
                            stats.failedRequests++;
                            errorCount++;
                        }

                        progressBar.update(stats.totalRequests.load());

                        // 添加小延迟避免过载
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    std::cerr << "Thread " << i << " fatal error: " << e.what() << std::endl;
                    errorCount += MAX_ERRORS;
                }
            });
        }

        // 等待所有线程完成
        auto threadTimeout = std::chrono::seconds(30);
        auto joinStart = std::chrono::steady_clock::now();

        for (auto& thread : threads) {
            if (thread.joinable()) {
                auto remainingTime = threadTimeout -
                                     (std::chrono::steady_clock::now() - joinStart);

                if (remainingTime <= std::chrono::seconds(0)) {
                    std::cerr << "Thread join timeout" << std::endl;
                    break;
                }

                thread.join();
            }
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                            endTime - startTime);

        std::cout << std::endl;
        stats.print(duration);

        if (errorCount.load() >= MAX_ERRORS) {
            std::cerr << "Test stopped due to too many errors" << std::endl;
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

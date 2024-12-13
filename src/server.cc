#include "tcp_server.h"
#include "logger.h"
#include <signal.h>
#include <string.h>


std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 初始化日志
    if (!Logger::instance().init("logs/server.log", LogLevel::DEBUG)) {
        std::cerr << "Init logger failed" << std::endl;
        return 1;
    }

    try {
        // 创建服务器实例
        TcpServer server("0.0.0.0", 8080, 4);

        // 设置消息处理回调
        server.setMessageCallback([](const std::shared_ptr<TcpConnection>& conn,
                                     const MessageHeader& header,
        const std::vector<char>& body) {
            // 将消息转换为字符串
            std::string message(body.begin(), body.end());
            LOG_INFO("Received message from fd {}: type={}, content={}",
                     conn->fd(), header.type, message);

            // 回显消息
            conn->sendMessage(header.type, message);
        });

        // 设置连接关闭回调
        server.setCloseCallback([](const std::shared_ptr<TcpConnection>& conn) {
            if (!conn) {
                return;
            }
            conn->close();
            LOG_INFO("Connection closed: fd={}", conn->fd());
        });

        // 设置错误处理回调
        server.setErrorCallback([](const std::shared_ptr<TcpConnection>& conn,
        const std::string& error) {
            LOG_ERROR("Connection error: fd={}, error={}", conn->fd(), error);
        });

        // 启动服务器
        server.start();

        // 等待信号
        LOG_INFO("Server started, press Ctrl+C to stop");


        // 主线程等待
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        LOG_INFO("Server stopping...");

    } catch (const std::exception& e) {
        LOG_ERROR("Server error: {}", e.what());
        return 1;
    }

    return 0;
}

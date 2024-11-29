#include <signal.h>
#include "tcp_server.h"
#include "logger.h"

class EchoServer {
  public:
    EchoServer(const std::string& ip, uint16_t port)
        : server_(ip, port) {

        // 设置消息处理回调
        server_.setMessageCallback(
            [this](const std::shared_ptr<TcpConnection>& conn,
                   const MessageHeader& header,
        const std::vector<char>& body) {
            // 简单的回显服务
            std::string msg(body.begin(), body.end());
            LOG_INFO("Received: {}", msg);

            // 构造响应消息
            MessageHeader respHeader;
            respHeader.length = body.size();
            respHeader.type = header.type;

            // 发送响应
            conn->send(respHeader, body);
        });
    }

    void start() {
        server_.start();
    }

  private:
    TcpServer server_;
};

int main() {
    Logger::instance().init("echo_server.log");

    try {
        EchoServer server("0.0.0.0", 8080);
        server.start();

        // 等待信号
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGTERM);

        int sig;
        sigwait(&sigset, &sig);

    } catch (const std::exception& e) {
        LOG_ERROR("Server error: {}", e.what());
        return 1;
    }

    return 0;
}

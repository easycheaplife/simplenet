#include "socket.h"
#include "message.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

class TestClient {
public:
    TestClient(const std::string& ip, uint16_t port)
        : socket_() {
        if (!socket_.create()) {
            throw std::runtime_error("Create socket failed");
        }
        
        if (!connect(ip, port)) {
            throw std::runtime_error("Connect failed");
        }
    }

	bool connect(const std::string& ip, uint16_t port) {
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = inet_addr(ip.c_str());

		// 设置为阻塞模式进行连接
		socket_.setBlocking();

		if (::connect(socket_.fd(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
			std::cerr << "Connect failed: " << strerror(errno) << std::endl;
			return false;
		}

		// 连接成功后再设置为非阻塞模式
		socket_.setNonBlocking();
		return true;
	}

    bool sendMessage(uint16_t type, const std::string& data) {
        MessageHeader header;
        header.magic = MAGIC_NUMBER;
        header.length = data.size();
        header.type = type;
        header.version = 1;

        // 发送头部
        if (socket_.write(&header, sizeof(header)) != sizeof(header)) {
            std::cerr << "Send header failed" << std::endl;
            return false;
        }

        // 发送数据
        if (socket_.write(data.data(), data.size()) != static_cast<ssize_t>(data.size())) {
            std::cerr << "Send data failed" << std::endl;
            return false;
		}

		return true;
	}

	bool receiveResponse() {
		MessageHeader header;
		size_t totalRead = 0;
		while (totalRead < sizeof(header)) {
			ssize_t n = socket_.read(
					reinterpret_cast<char*>(&header) + totalRead,
					sizeof(header) - totalRead
					);
			if (n > 0) {
				totalRead += n;
			} else if (n == 0) {
				std::cerr << "Connection closed by server" << std::endl;
				return false;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
				std::cerr << "Read header failed: " << strerror(errno) << std::endl;
				return false;
			}
		}

		if (header.magic != MAGIC_NUMBER) {
			std::cerr << "Invalid magic number" << std::endl;
			return false;
		}

		std::vector<char> data(header.length);
		totalRead = 0;
		while (totalRead < header.length) {
			ssize_t n = socket_.read(data.data() + totalRead, header.length - totalRead);
			if (n > 0) {
				totalRead += n;
			} else if (n == 0) {
				std::cerr << "Connection closed by server" << std::endl;
				return false;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
				std::cerr << "Read data failed: " << strerror(errno) << std::endl;
				return false;
			}
		}

		std::string response(data.begin(), data.end());
		std::cout << "Received response: type=" << header.type
			<< ", data=" << response << std::endl;
		return true;
    }

private:
    Socket socket_;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <ip> <port>" << std::endl;
        return 1;
    }

    try {
        TestClient client(argv[1], std::stoi(argv[2]));
        
        // 发送多条测试消息
        for (int i = 1; i <= 10; ++i) {
            std::string message = "Test message " + std::to_string(i);
            std::cout << "Sending: " << message << std::endl;
            
            if (!client.sendMessage(1, message)) {
                std::cerr << "Send message failed" << std::endl;
                return 1;
            }

            // 等待并接收响应
            if (!client.receiveResponse()) {
                std::cerr << "Receive response failed" << std::endl;
                return 1;
            }

            // 稍微延迟一下
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

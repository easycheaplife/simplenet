#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "message.h"


class EchoClient {
  public:
    EchoClient(const std::string& ip, uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            throw std::runtime_error("Create socket failed");
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd_);
            throw std::runtime_error("Connect failed");
        }
    }

    ~EchoClient() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    bool sendMessage(const std::string& data) {
        MessageHeader header;
        header.length = data.size();

        if (send(fd_, &header, sizeof(header), 0) != sizeof(header)) {
            return false;
        }

        if (send(fd_, data.c_str(), data.length(), 0) != static_cast<ssize_t>(data.length())) {
            return false;
        }

        return true;
    }

    bool receiveResponse() {
        MessageHeader header;
        if (recv(fd_, &header, sizeof(header), 0) != sizeof(header)) {
            return false;
        }

        if (header.magic != 0x12345678) {
            std::cerr << "Invalid magic number" << std::endl;
            return false;
        }

        std::vector<char> buffer(header.length);
        ssize_t received = 0;
        while (received < header.length) {
            ssize_t n = recv(fd_, buffer.data() + received, header.length - received, 0);
            if (n <= 0) {
                return false;
            }
            received += n;
        }

        std::string response(buffer.begin(), buffer.end());
        std::cout << "Received: type=" << header.type
                  << ", length=" << header.length
                  << ", data=" << response << std::endl;
        return true;
    }

  private:
    int fd_ = -1;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <ip> <port>" << std::endl;
        return 1;
    }

    try {
        EchoClient client(argv[1], std::stoi(argv[2]));

        std::string input;
        while (true) {
            std::cout << "Enter message (or 'quit' to exit): ";
            std::getline(std::cin, input);

            if (input == "quit") {
                break;
            }

            if (!client.sendMessage(input)) {
                std::cerr << "Send failed" << std::endl;
                break;
            }

            if (!client.receiveResponse()) {
                std::cerr << "Receive failed" << std::endl;
                break;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

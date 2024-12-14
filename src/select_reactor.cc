#include "select_reactor.h"

bool SelectReactor::init() {
    FD_ZERO(&readFds_);
    FD_ZERO(&writeFds_);
    maxFd_ = -1;
    return true;
}

void SelectReactor::poll(int timeoutMs) {
    fd_set readFdsCopy = readFds_;
    fd_set writeFdsCopy = writeFds_;

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(maxFd_ + 1, &readFdsCopy, &writeFdsCopy, nullptr, &tv);
    if (ret > 0) {
        for (int fd = 0; fd <= maxFd_; ++fd) {
            if (FD_ISSET(fd, &readFdsCopy)) {
                auto it = readCallbacks_.find(fd);
                if (it != readCallbacks_.end()) {
                    if (it->second) {
                        it->second();
                    }
                }
            }
            if (FD_ISSET(fd, &writeFdsCopy)) {
                auto it = writeCallbacks_.find(fd);
                if (it != writeCallbacks_.end()) {
                    if (it->second) {
                        it->second();
                    }
                }
            }
        }
    }
}

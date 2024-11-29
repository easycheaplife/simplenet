#pragma once
#include "reactor.h"
#include <sys/select.h>
#include "logger.h"

class SelectReactor : public Reactor {
  public:
    SelectReactor() = default;
    ~SelectReactor() override = default;

    bool init() override;
    void poll(int timeoutMs) override;

    bool addEvent(int fd, EventType type, const EventCallback& cb) override {
        if (fd < 0) {
            LOG_ERROR("Invalid fd: {}", fd);
            return false;
        }

        switch (type) {
        case EventType::READ:
            FD_SET(fd, &readFds_);
            readCallbacks_[fd] = cb;
            break;
        case EventType::WRITE:
            FD_SET(fd, &writeFds_);
            writeCallbacks_[fd] = cb;
            break;
        default:
            LOG_ERROR("Unsupported event type");
            return false;
        }

        maxFd_ = std::max(maxFd_, fd);
        return true;
    }

    bool removeEvent(int fd, EventType type) override {
        if (fd < 0) {
            LOG_ERROR("Invalid fd: {}", fd);
            return false;
        }

        switch (type) {
        case EventType::READ:
            FD_CLR(fd, &readFds_);
            readCallbacks_.erase(fd);
            break;
        case EventType::WRITE:
            FD_CLR(fd, &writeFds_);
            writeCallbacks_.erase(fd);
            break;
        default:
            LOG_ERROR("Unsupported event type");
            return false;
        }

        // 更新maxFd_
        if (fd == maxFd_) {
            while (maxFd_ >= 0 &&
                    !FD_ISSET(maxFd_, &readFds_) &&
                    !FD_ISSET(maxFd_, &writeFds_)) {
                --maxFd_;
            }
        }
        return true;
    }

    bool updateEvent(int fd, EventType type) override {
        removeEvent(fd, type);
        return addEvent(fd, type, type == EventType::READ ?
                        readCallbacks_[fd] : writeCallbacks_[fd]);
    }

  private:
    fd_set readFds_;
    fd_set writeFds_;
    int maxFd_{-1};
};

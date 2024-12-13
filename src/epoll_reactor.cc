#include "epoll_reactor.h"
#include <unistd.h>
#include <string.h>
#include "logger.h"

EpollReactor::EpollReactor() : events_(MAX_EVENTS) {}

EpollReactor::~EpollReactor() {
    if (epollFd_ >= 0) {
        close(epollFd_);
    }
}

bool EpollReactor::init() {
    if (epollFd_ >= 0) {
        close(epollFd_);
    }

    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        LOG_ERROR("epoll_create1 failed: {}", strerror(errno));
        return false;
    }
    LOG_INFO("EpollReactor initialized with epollFd: {}", epollFd_);
    return true;
}

void EpollReactor::poll(int timeoutMs) {
    std::vector<epoll_event> activeEvents = events_;
    int numEvents = epoll_wait(epollFd_, activeEvents.data(), MAX_EVENTS, timeoutMs);

    if (numEvents < 0) {
        if (errno != EINTR) {
            LOG_ERROR("epoll_wait failed: {}", strerror(errno));
        }
        return;
    }

    std::vector<std::pair<int, EventCallback>> pendingCallbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < numEvents; ++i) {
            int fd = activeEvents[i].data.fd;
            uint32_t events = activeEvents[i].events;
            if (events & (EPOLLERR | EPOLLHUP)) {
                LOG_WARN("epoll error on fd: {}", fd);
                if (close(fd) < 0) {
                    LOG_ERROR("Failed to close fd {}: {}", fd, strerror(errno));
                } else {
                    LOG_INFO("Closed fd {} due to EPOLLERR/EPOLLHUP", fd);
                }
                clearEvents(fd);
                continue;
            }

            if (events & EPOLLIN) {
                auto it = readCallbacks_.find(fd);
                if (it != readCallbacks_.end()) {
                    pendingCallbacks.emplace_back(fd, it->second);
                }
            }

            if (events & EPOLLOUT) {
                auto it = writeCallbacks_.find(fd);
                if (it != writeCallbacks_.end()) {
                    pendingCallbacks.emplace_back(fd, it->second);
                }
            }
        }
    }

    for (const auto& callback_pair : pendingCallbacks) {
        const int fd = callback_pair.first;
        const EventCallback& callback = callback_pair.second;
        if (callback) {
            try {
                callback();
            } catch (const std::exception& e) {
                LOG_ERROR("Callback exception for fd {}: {}", fd, e.what());
                std::lock_guard<std::mutex> lock(mutex_);
                readCallbacks_.erase(fd);
                writeCallbacks_.erase(fd);
            }
        }
    }
}

bool EpollReactor::addEvent(int fd, EventType type, const EventCallback& cb) {
    if (fd < 0 || epollFd_ < 0) {
        LOG_ERROR("Invalid fd: {} or epollFd: {}", fd, epollFd_);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    struct epoll_event ev {};
    ev.data.fd = fd;

    auto it = fdEvents_.find(fd);
    uint32_t events = (it != fdEvents_.end()) ? it->second : 0;

    if (type == EventType::READ) {
        events |= (EPOLLIN | EPOLLET);
        readCallbacks_[fd] = cb;
    } else if (type == EventType::WRITE) {
        events |= (EPOLLOUT | EPOLLET);
        writeCallbacks_[fd] = cb;
    }

    ev.events = events;

    int op = (it != fdEvents_.end()) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    LOG_INFO("Adding/updating event for fd: {} (epollFd: {}, op: {})",
             fd, epollFd_, op == EPOLL_CTL_ADD ? "ADD" : "MOD");

    if (epoll_ctl(epollFd_, op, fd, &ev) < 0) {
        // 如果文件描述符不存在，尝试添加而不是修改
        if (errno == ENOENT && op == EPOLL_CTL_MOD) {
            op = EPOLL_CTL_ADD;
            if (epoll_ctl(epollFd_, op, fd, &ev) < 0) {
                LOG_ERROR("epoll_ctl add failed for fd {} (epollFd: {}): {}",
                          fd, epollFd_, strerror(errno));
                return false;
            }
        } else {
            LOG_ERROR("epoll_ctl {} failed for fd {} (epollFd: {}): {}",
                      op == EPOLL_CTL_ADD ? "add" : "mod",
                      fd, epollFd_, strerror(errno));
            return false;
        }
    }

    fdEvents_[fd] = events;
    LOG_INFO("Successfully added/updated event for fd: {} (epollFd: {})", fd, epollFd_);
    return true;
}

bool EpollReactor::removeEvent(int fd, EventType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fdEvents_.find(fd);
    if (it == fdEvents_.end()) {
        // 如果文件描述符不在事件表中，视为成功
        return true;
    }

    uint32_t events = it->second;
    if (type == EventType::READ) {
        events &= ~EPOLLIN;
        readCallbacks_.erase(fd);
    } else if (type == EventType::WRITE) {
        events &= ~EPOLLOUT;
        writeCallbacks_.erase(fd);
    }
    events &= ~EPOLLET;

    if (events == 0) {
        // 如果没有任何事件，直接删除
        fdEvents_.erase(it);
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            if (errno != ENOENT && errno != EBADF) {
                LOG_ERROR("epoll_ctl del failed: {}", strerror(errno));
                return false;
            }
        }
    } else {
        // 更新事件
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        fdEvents_[fd] = events;
        if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
            if (errno != ENOENT && errno != EBADF) {
                LOG_ERROR("epoll_ctl mod failed in removeEvent: {}", strerror(errno));
                return false;
            }
        }
    }
    return true;
}

bool EpollReactor::updateEvent(int fd, EventType type) {
    auto it = fdEvents_.find(fd);
    if (it == fdEvents_.end()) {
        return false;
    }

    uint32_t events = it->second;
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl mod failed in updateEvent: {}", strerror(errno));
        return false;
    }
    return true;
}

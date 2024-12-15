#pragma once
#include "reactor.h"
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

class EpollReactor : public Reactor {
  public:
    EpollReactor();
    ~EpollReactor() override;

    bool init() override;
    void poll(int timeoutMs) override;
    bool addEvent(int fd, EventType type, const EventCallback& cb) override;
    bool removeEvent(int fd, EventType type) override;
    bool updateEvent(int fd, EventType type) override;
  private:
    void clearEvents(int fd) {
        fdEvents_.erase(fd);
        readCallbacks_.erase(fd);
        writeCallbacks_.erase(fd);
        if (epollFd_ >= 0) {
            epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        }
    }

  private:
    int epollFd_{-1};
    std::vector<epoll_event> events_;
    std::unordered_map<int, uint32_t> fdEvents_;
    static const int MAX_EVENTS = 1024;
    static constexpr int MAX_READ_RETRIES = 3;
};

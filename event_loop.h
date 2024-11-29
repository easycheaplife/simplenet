#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "reactor.h"

class EventLoop {
  public:
    EventLoop();
    ~EventLoop();

    void start();
    void stop();

    bool addEvent(int fd, Reactor::EventType type,
                  const Reactor::EventCallback& cb);

    bool removeEvent(int fd, Reactor::EventType type) {
        return reactor_->removeEvent(fd, type);
    }

  private:
    void loop();

    std::unique_ptr<Reactor> reactor_;
    std::thread loopThread_;
    std::atomic<bool> running_{false};
};

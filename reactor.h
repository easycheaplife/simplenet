#pragma once
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>

class Reactor {
  public:
    using EventCallback = std::function<void()>;

    enum class EventType {
        READ = 0x01,
        WRITE = 0x02,
        ERROR = 0x04
    };

    virtual ~Reactor() = default;
    virtual bool init() = 0;
    virtual bool addEvent(int fd, EventType type, const EventCallback& cb) = 0;
    virtual bool removeEvent(int fd, EventType type) = 0;
    virtual bool updateEvent(int fd, EventType type) = 0;
    virtual void poll(int timeoutMs) = 0;

  protected:
    std::unordered_map<int, EventCallback> readCallbacks_;
    std::unordered_map<int, EventCallback> writeCallbacks_;
	mutable std::mutex mutex_;
};

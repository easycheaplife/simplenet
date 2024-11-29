#include "event_loop.h"
#include "logger.h"

#ifdef __linux__
    #include "epoll_reactor.h"
    #define DEFAULT_REACTOR std::make_unique<EpollReactor>()
#else
    #include "select_reactor.h"
    #define DEFAULT_REACTOR std::make_unique<SelectReactor>()
#endif

EventLoop::EventLoop() 
    : reactor_(DEFAULT_REACTOR)
    , running_(false) {
    if (!reactor_->init()) {
        LOG_ERROR("Reactor init failed");
        throw std::runtime_error("Reactor init failed");
    }
}

EventLoop::~EventLoop() {
    stop();
}

void EventLoop::start() {
    if (running_) return;
    
    running_ = true;
    loopThread_ = std::thread(&EventLoop::loop, this);
}

void EventLoop::stop() {
    running_ = false;
    if (loopThread_.joinable()) {
        loopThread_.join();
    }
}

void EventLoop::loop() {
    while (running_) {
        reactor_->poll(100); // 100ms timeout
    }
}

bool EventLoop::addEvent(int fd, Reactor::EventType type, 
                        const Reactor::EventCallback& cb) {
    if (fd < 0) {
        LOG_ERROR("Invalid fd in addEvent: {}", fd);
        return false;
    }
    return reactor_->addEvent(fd, type, cb);
}

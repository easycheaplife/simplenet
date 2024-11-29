#pragma once
#include <vector>
#include <cstring>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 8192)
        : buffer_(capacity), readPos_(0), writePos_(0) {}

    size_t readableBytes() const {
        return writePos_ - readPos_;
    }

    size_t writableBytes() const {
        return buffer_.size() - writePos_;
    }

    bool peek(void* data, size_t len) {
        if (readableBytes() < len) {
            return false;
        }
        memcpy(data, &buffer_[readPos_], len);
        return true;
    }

    void retrieve(size_t len) {
        if (len < readableBytes()) {
            readPos_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveAll() {
        readPos_ = writePos_ = 0;
    }

    const char* peek() const {
        return &buffer_[readPos_];
    }

    size_t read(char* data, size_t len) {
        if (len > readableBytes()) {
            len = readableBytes();
        }
        memcpy(data, &buffer_[readPos_], len);
        retrieve(len);
        return len;
    }

    size_t write(const char* data, size_t len) {
        ensureWritableBytes(len);
        memcpy(&buffer_[writePos_], data, len);
        writePos_ += len;
        return len;
    }

private:
    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
    }

    void makeSpace(size_t len) {
        if (writableBytes() + readPos_ < len) {
            buffer_.resize(writePos_ + len);
        } else {
            size_t readable = readableBytes();
            std::copy(&buffer_[readPos_],
                     &buffer_[writePos_],
                     &buffer_[0]);
            readPos_ = 0;
            writePos_ = readable;
        }
    }

    std::vector<char> buffer_;
    size_t readPos_;
    size_t writePos_;
};

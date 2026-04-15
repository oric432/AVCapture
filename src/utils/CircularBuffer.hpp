#pragma once

#include <deque>
#include <mutex>
#include <utility>
#include <cstddef>
#include <vector>

namespace VSCapture::Utils {

template <typename T, typename Container = std::deque<T>>
class CircularBuffer {
public:
    using ValueType = T;
    using ContainerType = Container;
    using SizeType = std::size_t;

    explicit CircularBuffer(SizeType capacity = 0)
        : capacity_(capacity) {}

    ~CircularBuffer() = default;

    // Disable copying to avoid accidental heavy copies of big buffers
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;

    CircularBuffer(CircularBuffer&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        buffer_ = std::move(other.buffer_); // NOLINT
        capacity_ = other.capacity_; // NOLINT
    }

    CircularBuffer& operator=(CircularBuffer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        std::lock(mutex_, other.mutex_);
        std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);

        buffer_ = std::move(other.buffer_);
        capacity_ = other.capacity_;
        return *this;
    }

    void set_capacity(SizeType new_capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = new_capacity;

        while (capacity_ > 0 && buffer_.size() > capacity_) {
            buffer_.pop_front();
        }
    }

    SizeType capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }

    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(value);
        enforce_capacity();
    }

    void push(T&& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(std::move(value));
        enforce_capacity();
    }

    template <typename... Args>
    void emplace(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.emplace_back(std::forward<Args>(args)...);
        enforce_capacity();
    }

    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) {
            return false;
        }

        out = std::move(buffer_.front());
        buffer_.pop_front();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.empty();
    }

    SizeType size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    std::vector<T> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<T>(buffer_.begin(), buffer_.end());
    }

    template <typename Func>
    void for_each(Func&& func) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& elem : buffer_) {
            func(elem);
        }
    }

private:
    void enforce_capacity() {
        if (capacity_ == 0) {
            return;
        }
        if (buffer_.size() > capacity_) {
            buffer_.pop_front();
        }
    }

    mutable std::mutex mutex_;
    Container buffer_;
    SizeType capacity_;
};

} // namespace VSCapture::Utils
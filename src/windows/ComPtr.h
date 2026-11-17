#pragma once

#include <unknwn.h>

#include <utility>

namespace st {

// Minimal COM smart pointer; avoids pulling in a COM helper library.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)) {}

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] T* Get() const noexcept { return pointer_; }

    T** Put() noexcept {
        Reset();
        return &pointer_;
    }

    T* operator->() const noexcept { return pointer_; }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }

    void Attach(T* pointer) noexcept {
        Reset();
        pointer_ = pointer;
    }

    void Reset() noexcept {
        if (pointer_ != nullptr) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

private:
    T* pointer_ = nullptr;
};

} // namespace st

#include "Config.h"

#include "Log.h"

#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace st {
namespace {

constexpr int kDebounceMs = 300;

} // namespace

struct ConfigWatcher::Impl {
    int queueFd = -1;
    int directoryFd = -1;
    std::function<void()> onChanged;
    std::jthread thread;
};

ConfigWatcher::ConfigWatcher() : impl_(std::make_unique<Impl>()) {}

ConfigWatcher::~ConfigWatcher() {
    Stop();
}

bool ConfigWatcher::Watch(const std::filesystem::path& configPath,
                          std::function<void()> onChanged) {
    Stop();

    if (!configPath.has_parent_path()) {
        return false;
    }

    // kqueue only reports "something in this directory changed"; the debounce
    // makes that one reload, and a reload is idempotent.
    const int directoryFd = open(configPath.parent_path().c_str(), O_RDONLY | O_EVTONLY);
    if (directoryFd < 0) {
        LogFormat(LogLevel::Warning, L"Config watcher: cannot open \"%ls\" (%hs).",
                  configPath.parent_path().c_str(), std::strerror(errno));
        return false;
    }

    const int queueFd = kqueue();
    if (queueFd < 0) {
        close(directoryFd);
        return false;
    }

    struct kevent change {};
    EV_SET(&change, static_cast<uintptr_t>(directoryFd), EVFILT_VNODE,
           EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_RENAME | NOTE_EXTEND, 0, nullptr);
    if (kevent(queueFd, &change, 1, nullptr, 0, nullptr) < 0) {
        close(queueFd);
        close(directoryFd);
        return false;
    }

    impl_->queueFd = queueFd;
    impl_->directoryFd = directoryFd;
    impl_->onChanged = std::move(onChanged);
    impl_->thread = std::jthread([this](std::stop_token token) { Run(token); });
    return true;
}

void ConfigWatcher::Stop() {
    if (!impl_) {
        return;
    }

    if (impl_->thread.joinable()) {
        impl_->thread.request_stop();
        impl_->thread.join();
    }

    if (impl_->queueFd >= 0) {
        close(impl_->queueFd);
        impl_->queueFd = -1;
    }
    if (impl_->directoryFd >= 0) {
        close(impl_->directoryFd);
        impl_->directoryFd = -1;
    }
    impl_->onChanged = nullptr;
}

void ConfigWatcher::Run(std::stop_token token) {
    bool matched = false;

    while (!token.stop_requested()) {
        struct kevent event {};
        timespec timeout{};
        timeout.tv_sec = kDebounceMs / 1000;
        timeout.tv_nsec = (kDebounceMs % 1000) * 1000000L;

        const int ready = kevent(impl_->queueFd, nullptr, 0, &event, 1, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            if (matched) {
                matched = false;
                if (impl_->onChanged) {
                    impl_->onChanged();
                }
            }
            continue;
        }

        matched = true;
    }
}

} // namespace st

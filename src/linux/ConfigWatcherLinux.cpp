#include "Config.h"

#include "Log.h"
#include "Text.h"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace st {
namespace {

constexpr int kDebounceMs = 300;

} // namespace

struct ConfigWatcher::Impl {
    int inotifyFd = -1;
    int watchDescriptor = -1;
    std::string fileName;
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

    const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (fd < 0) {
        LogFormat(LogLevel::Warning,
                  L"Config watcher: inotify_init1 failed (%hs).", std::strerror(errno));
        return false;
    }

    const int descriptor =
        inotify_add_watch(fd, configPath.parent_path().c_str(),
                          IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_CREATE);
    if (descriptor < 0) {
        LogFormat(LogLevel::Warning,
                  L"Config watcher: cannot watch \"%ls\" (%hs).",
                  configPath.parent_path().c_str(), std::strerror(errno));
        close(fd);
        return false;
    }

    impl_->inotifyFd = fd;
    impl_->watchDescriptor = descriptor;
    impl_->fileName = text::ToUtf8(configPath.filename().wstring());
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

    if (impl_->inotifyFd >= 0) {
        if (impl_->watchDescriptor >= 0) {
            inotify_rm_watch(impl_->inotifyFd, impl_->watchDescriptor);
        }
        close(impl_->inotifyFd);
        impl_->inotifyFd = -1;
        impl_->watchDescriptor = -1;
    }
    impl_->onChanged = nullptr;
}

void ConfigWatcher::Run(std::stop_token token) {
    bool matched = false;

    while (!token.stop_requested()) {
        pollfd descriptor{};
        descriptor.fd = impl_->inotifyFd;
        descriptor.events = POLLIN;

        const int ready = poll(&descriptor, 1, kDebounceMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            // Quiet period elapsed: one reload per burst of writes.
            if (matched) {
                matched = false;
                if (impl_->onChanged) {
                    impl_->onChanged();
                }
            }
            continue;
        }

        char buffer[4096];
        const ssize_t bytes = read(impl_->inotifyFd, buffer, sizeof(buffer));
        if (bytes <= 0) {
            if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            break;
        }

        ssize_t offset = 0;
        while (offset + static_cast<ssize_t>(sizeof(inotify_event)) <= bytes) {
            const auto* event =
                reinterpret_cast<const inotify_event*>(buffer + offset);
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);

            if ((event->mask & IN_IGNORED) != 0) {
                return;
            }
            if (event->len == 0) {
                continue;
            }
            if (event->name == impl_->fileName ||
                std::strcmp(event->name, impl_->fileName.c_str()) == 0) {
                matched = true;
            }
        }
    }
}

} // namespace st

#include "config_watcher.hpp"
#include "config.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>

namespace modguard {
namespace {

// Hard-coded poll interval while waiting for the file to appear.
constexpr int kFileWaitMs = 1000;

// inotify event mask. We watch the file itself (added after it appears).
constexpr std::uint32_t kWatchMask = IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF | IN_ATTRIB;

// Read buffer for inotify events. A single 16-byte file close event fits
// easily; we never need a multi-event read here because we only act on the
// first event and re-poll on the next loop iteration.
constexpr std::size_t kEventBufSize = 256;

bool file_exists(const std::string &path)
{
    struct stat buffer {};
    return ::stat(path.c_str(), &buffer) == 0;
}

} // namespace

ConfigWatcher::ConfigWatcher(std::string path, scs_log_t log, ConfigSlot &slot)
    : path_(std::move(path)), log_(log), slot_(slot)
{
}

ConfigWatcher::~ConfigWatcher()
{
    stop();
}

void ConfigWatcher::log(scs_log_type_t type, const std::string &message)
{
    if (log_) {
        log_(type, message.c_str());
    }
}

void ConfigWatcher::start()
{
    if (running_.load(std::memory_order_acquire)) {
        return;
    }

    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        log(SCS_LOG_TYPE_warning, "[MODGuard Linux] Config watcher could not initialize inotify");
        return;
    }

    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
        ::close(inotify_fd_);
        inotify_fd_ = -1;
        log(SCS_LOG_TYPE_warning, "[MODGuard Linux] Config watcher could not initialize eventfd");
        return;
    }

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::make_unique<std::thread>([this] { run(); });
}

void ConfigWatcher::stop()
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    stop_.store(true, std::memory_order_release);
    if (wake_fd_ >= 0) {
        // Wake the read() so it returns immediately.
        const std::uint64_t one = 1;
        (void)::write(wake_fd_, &one, sizeof(one));
    }
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
    if (watch_descriptor_ >= 0) {
        ::inotify_rm_watch(inotify_fd_, watch_descriptor_);
        watch_descriptor_ = -1;
    }
    if (wake_fd_ >= 0) {
        ::close(wake_fd_);
        wake_fd_ = -1;
    }
    if (inotify_fd_ >= 0) {
        ::close(inotify_fd_);
        inotify_fd_ = -1;
    }
    running_.store(false, std::memory_order_release);
}

void ConfigWatcher::reload_now()
{
    LoadedConfig loaded = load_config_file(path_);
    if (!loaded.found) {
        log(SCS_LOG_TYPE_warning,
            "[MODGuard Linux] Config file missing; keeping previous configuration");
        return;
    }
    slot_.publish(new ExhaustConfig(loaded.exhaust));
    log(SCS_LOG_TYPE_message,
        "[MODGuard Linux] Configuration reloaded from " + loaded.path);
}

void ConfigWatcher::run()
{
    // Phase 1: wait for the file to exist. inotify requires a real path.
    while (!stop_.load(std::memory_order_acquire)) {
        if (file_exists(path_)) {
            break;
        }
        // Sleep on the wake fd with a timeout so stop() is honored quickly.
        pollfd pfd{};
        pfd.fd = wake_fd_;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, kFileWaitMs);
        if (rc > 0 && (pfd.revents & POLLIN) != 0) {
            std::uint64_t drain = 0;
            (void)::read(wake_fd_, &drain, sizeof(drain));
        }
    }
    if (stop_.load(std::memory_order_acquire)) {
        return;
    }

    // Phase 2: add the inotify watch and process events.
    watch_descriptor_ = ::inotify_add_watch(inotify_fd_, path_.c_str(), kWatchMask);
    if (watch_descriptor_ < 0) {
        log(SCS_LOG_TYPE_warning,
            "[MODGuard Linux] Config watcher could not watch " + path_);
        return;
    }

    // Initial reload: pick up any content already on disk at startup.
    reload_now();

    char buffer[kEventBufSize];
    while (!stop_.load(std::memory_order_acquire)) {
        pollfd pfds[2]{};
        pfds[0].fd = inotify_fd_;
        pfds[0].events = POLLIN;
        pfds[1].fd = wake_fd_;
        pfds[1].events = POLLIN;
        const int rc = ::poll(pfds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if ((pfds[1].revents & POLLIN) != 0) {
            std::uint64_t drain = 0;
            (void)::read(wake_fd_, &drain, sizeof(drain));
        }
        if ((pfds[0].revents & POLLIN) != 0) {
            const ssize_t bytes = ::read(inotify_fd_, buffer, sizeof(buffer));
            if (bytes <= 0) {
                if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                    continue;
                }
                break;
            }
            // We act on the presence of any event; specifics are not needed
            // because we always re-read the file from disk.
            bool handled = false;
            bool vanished = false;
            for (ssize_t offset = 0; offset + static_cast<ssize_t>(sizeof(inotify_event)) <= bytes;
                 offset += sizeof(inotify_event) + reinterpret_cast<const inotify_event *>(
                                                    buffer + offset)->len) {
                const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
                if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) {
                    vanished = true;
                }
                handled = true;
            }
            if (handled) {
                // For atomic-rename saves, the file may briefly be a different
                // inode or absent. Drop the watch and let the next loop
                // iteration re-add it (poll-then-watch).
                if (vanished && watch_descriptor_ >= 0) {
                    ::inotify_rm_watch(inotify_fd_, watch_descriptor_);
                    watch_descriptor_ = -1;
                }
                if (!vanished) {
                    reload_now();
                } else if (stop_.load(std::memory_order_acquire)) {
                    // Skip re-arm; stop is in flight.
                    break;
                } else {
                    // Re-arm below.
                }
            }
            if (vanished) {
                // Spin until the file reappears, then re-add the watch.
                while (!stop_.load(std::memory_order_acquire)) {
                    if (file_exists(path_)) {
                        break;
                    }
                    pollfd pfd{};
                    pfd.fd = wake_fd_;
                    pfd.events = POLLIN;
                    const int prc = ::poll(&pfd, 1, kFileWaitMs);
                    if (prc > 0 && (pfd.revents & POLLIN) != 0) {
                        std::uint64_t drain = 0;
                        (void)::read(wake_fd_, &drain, sizeof(drain));
                    }
                }
                if (stop_.load(std::memory_order_acquire)) {
                    break;
                }
                watch_descriptor_ = ::inotify_add_watch(inotify_fd_, path_.c_str(), kWatchMask);
                if (watch_descriptor_ < 0) {
                    log(SCS_LOG_TYPE_warning,
                        "[MODGuard Linux] Config watcher could not re-watch " + path_);
                    break;
                }
                if (stop_.load(std::memory_order_acquire)) {
                    // Stop raced the re-arm; do not publish a reload that
                    // no consumer will pick up.
                    break;
                }
                reload_now();
            }
        }
    }
}

} // namespace modguard
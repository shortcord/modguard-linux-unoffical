#ifndef MODGUARD_CONFIG_WATCHER_HPP
#define MODGUARD_CONFIG_WATCHER_HPP

#include "config_runtime.hpp"
#include "scs_sdk_minimal.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace modguard {

// Filesystem watcher for the modguard_linux.ini config file.
//
// At startup, polls for the file to exist (inotify cannot watch a path that
// does not yet exist). Once present, watches the file directly with inotify
// for IN_CLOSE_WRITE (editor save), IN_MOVE_SELF / IN_DELETE_SELF (atomic
// rename save), and IN_ATTRIB (metadata change).
//
// On a relevant event, re-parses the file off-thread and publishes the new
// ExhaustConfig into the shared slot. The plugin thread applies it on the
// next frame_end.
//
// Bad reloads (missing file, parse error, sanitize rejection) are logged
// through game_log and the previous config is kept.
class ConfigWatcher {
public:
    ConfigWatcher(std::string path, scs_log_t log, ConfigSlot &slot);
    ~ConfigWatcher();

    ConfigWatcher(const ConfigWatcher &) = delete;
    ConfigWatcher &operator=(const ConfigWatcher &) = delete;

    // Start the watcher thread. Idempotent.
    void start();

    // Stop the watcher thread. Joins before returning. Idempotent.
    void stop();

    // True between successful start and stop completion. Used by tests and
    // for log diagnostics.
    bool running() const { return running_.load(std::memory_order_acquire); }

private:
    void run();
    void reload_now();
    void log(scs_log_type_t type, const std::string &message);

    std::string path_;
    scs_log_t log_;
    ConfigSlot &slot_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};

    int inotify_fd_{-1};
    int wake_fd_{-1};
    int watch_descriptor_{-1};
    std::unique_ptr<std::thread> thread_;
};

} // namespace modguard

#endif
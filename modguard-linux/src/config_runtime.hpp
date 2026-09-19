#ifndef MODGUARD_CONFIG_RUNTIME_HPP
#define MODGUARD_CONFIG_RUNTIME_HPP

#include "exhaust_model.hpp"

#include <atomic>

namespace modguard {

// Single-slot pending config slot used by the config watcher.
//
// The watcher thread publishes a heap-allocated ExhaustConfig when the ini
// file changes. The plugin thread consumes it at the top of frame_end.
//
// Ownership flow:
//   watcher thread: build new config, publish_config(next)
//   plugin thread:  take_pending_config() returns next (or nullptr),
//                   apply it, then `delete next`.
// Only one config is queued at a time; later publishes overwrite the slot
// before the consumer takes it. This coalesces bursts of file edits.
//
// If shutdown occurs with a pending config still in the slot, the destructor
// deletes it to avoid a leak.
class ConfigSlot {
public:
    ConfigSlot();
    ~ConfigSlot();
    ConfigSlot(const ConfigSlot &) = delete;
    ConfigSlot &operator=(const ConfigSlot &) = delete;

    // Watcher thread: publish a new pending config. Takes ownership of
    // `next`. If a previous config was pending and not yet consumed, it is
    // deleted here so memory cannot grow under rapid edits.
    void publish(ExhaustConfig *next);

    // Plugin thread: take ownership of the pending config (or nullptr).
    ExhaustConfig *take();

private:
    std::atomic<ExhaustConfig *> pending_{nullptr};
};

} // namespace modguard

#endif
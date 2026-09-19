#include "config_runtime.hpp"

namespace modguard {

ConfigSlot::ConfigSlot() = default;

ConfigSlot::~ConfigSlot()
{
    // The plugin must drain the slot via take() before destroying it. If it
    // did not, free whatever is still pending so we do not leak.
    if (ExhaustConfig *leftover = pending_.exchange(nullptr)) {
        delete leftover;
    }
}

void ConfigSlot::publish(ExhaustConfig *next)
{
    // Single-slot coalescing: any earlier unconsumed entry is discarded.
    // The watcher allocates a fresh copy for each successful reload, so a
    // missed entry simply means the consumer applies the latest one.
    ExhaustConfig *previous = pending_.exchange(next);
    if (previous) {
        delete previous;
    }
}

ExhaustConfig *ConfigSlot::take()
{
    return pending_.exchange(nullptr);
}

} // namespace modguard
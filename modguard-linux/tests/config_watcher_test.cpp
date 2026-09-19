// Tests for ConfigSlot and ConfigWatcher.
//
// Skips the inotify-driven tests gracefully if /proc/sys/fs/inotify is not
// available (e.g., inside a restricted container), since inotify is a
// kernel-level facility that requires /proc and a real filesystem.

#include "config_runtime.hpp"
#include "config_watcher.hpp"
#include "exhaust_model.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <sys/stat.h>

namespace {

bool inotify_available()
{
    struct stat buffer {};
    return ::stat("/proc/sys/fs/inotify/max_user_watches", &buffer) == 0;
}

void write_file(const std::string &path, const std::string &contents)
{
    std::ofstream output(path, std::ios::trunc);
    output << contents;
    output.close();
}

// Drains the slot until a published value matches `pred`, returning the
// matching value (caller deletes) or nullptr on timeout. Values that do not
// match are deleted so the slot is not corrupted by an out-of-order publish.
const modguard::ExhaustConfig *wait_for_publish(
    modguard::ConfigSlot &slot,
    bool (*pred)(const modguard::ExhaustConfig &),
    std::chrono::steady_clock::duration timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        modguard::ExhaustConfig *value = slot.take();
        if (!value) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (pred(*value)) {
            return value;
        }
        delete value;
    }
    return nullptr;
}

bool intensity_in(float lo, float hi, const modguard::ExhaustConfig &c)
{
    return c.intensity > lo && c.intensity < hi;
}

bool intensity_eq_default(const modguard::ExhaustConfig &c)
{
    return c.intensity == 0.3F;
}

} // namespace

int main()
{
    // --- ConfigSlot: single-slot coalescing and ownership ---
    {
        modguard::ConfigSlot slot;
        modguard::ExhaustConfig a{};
        a.intensity = 0.1F;
        slot.publish(new modguard::ExhaustConfig(a));

        modguard::ExhaustConfig b{};
        b.intensity = 0.2F;
        slot.publish(new modguard::ExhaustConfig(b));

        modguard::ExhaustConfig *taken = slot.take();
        assert(taken != nullptr);
        assert(taken->intensity > 0.19F && taken->intensity < 0.21F);
        delete taken;

        assert(slot.take() == nullptr);
    }

    if (!inotify_available()) {
        std::printf("[skip] inotify not available in this environment\n");
        return 0;
    }

    // --- ConfigWatcher: picks up file when it appears later ---
    {
        const std::string path = "/tmp/modguard-watcher-test-" +
                                 std::to_string(::getpid()) + ".ini";
        ::unlink(path.c_str());

        modguard::ConfigSlot slot;
        modguard::ConfigWatcher watcher(path, nullptr, slot);
        watcher.start();
        assert(watcher.running());

        // Wait up to 3 s for the file to appear in the polling phase.
        write_file(path, "[RealisticSmoke]\nIntensity=0.42\n");
        const modguard::ExhaustConfig *taken = wait_for_publish(
            slot, [](const auto &c) { return intensity_in(0.41F, 0.43F, c); },
            std::chrono::seconds(3));
        assert(taken != nullptr);
        delete taken;

        watcher.stop();
        assert(!watcher.running());
        ::unlink(path.c_str());
    }

    // --- ConfigWatcher: live edit reloads the config ---
    {
        const std::string path = "/tmp/modguard-watcher-test-" +
                                 std::to_string(::getpid()) + "-2.ini";
        write_file(path, "[RealisticSmoke]\nIntensity=0.10\n");

        modguard::ConfigSlot slot;
        modguard::ConfigWatcher watcher(path, nullptr, slot);
        watcher.start();

        // Wait for the initial reload, then edit and wait for the second.
        const modguard::ExhaustConfig *initial = wait_for_publish(
            slot, [](const auto &c) { return intensity_in(0.09F, 0.11F, c); },
            std::chrono::seconds(2));
        assert(initial != nullptr);
        delete initial;

        write_file(path, "[RealisticSmoke]\nIntensity=0.55\n");

        const modguard::ExhaustConfig *edited = wait_for_publish(
            slot, [](const auto &c) { return intensity_in(0.54F, 0.56F, c); },
            std::chrono::seconds(3));
        assert(edited != nullptr);
        delete edited;

        watcher.stop();
        ::unlink(path.c_str());
    }

    // --- ConfigWatcher: bad-value reload publishes defaults (chosen semantic) ---
    {
        const std::string path = "/tmp/modguard-watcher-test-" +
                                 std::to_string(::getpid()) + "-3.ini";
        write_file(path, "[RealisticSmoke]\nIntensity=0.30\n");

        modguard::ConfigSlot slot;
        modguard::ConfigWatcher watcher(path, nullptr, slot);
        watcher.start();

        const modguard::ExhaustConfig *initial = wait_for_publish(
            slot, [](const auto &c) { return intensity_in(0.29F, 0.31F, c); },
            std::chrono::seconds(2));
        assert(initial != nullptr);
        delete initial;

        // Bad-value keys fall back to defaults; the watcher publishes a
        // default-valued ExhaustConfig. The semantic is "publish what was
        // parsed, do not retain previous". This test verifies the watcher
        // did not crash and is still running after a bad edit.
        write_file(path, "[RealisticSmoke]\nIntensity=not-a-number\n");

        const modguard::ExhaustConfig *after_bad = wait_for_publish(
            slot, intensity_eq_default, std::chrono::seconds(2));
        assert(after_bad != nullptr);
        delete after_bad;
        assert(watcher.running());

        watcher.stop();
        ::unlink(path.c_str());
    }

    // --- ConfigWatcher: stop is prompt while blocked in phase-1 poll-for-file ---
    {
        const std::string path = "/tmp/modguard-watcher-test-" +
                                 std::to_string(::getpid()) + "-4.ini";
        ::unlink(path.c_str());

        modguard::ConfigSlot slot;
        modguard::ConfigWatcher watcher(path, nullptr, slot);
        watcher.start();
        assert(watcher.running());

        // Now in the poll-for-file phase; stop should return within a second.
        auto t0 = std::chrono::steady_clock::now();
        watcher.stop();
        auto elapsed = std::chrono::steady_clock::now() - t0;
        assert(elapsed < std::chrono::seconds(2));
        assert(!watcher.running());
    }

    // --- ConfigWatcher: stop is prompt while blocked in vanish-rearm spin ---
    {
        const std::string path = "/tmp/modguard-watcher-test-" +
                                 std::to_string(::getpid()) + "-5.ini";
        write_file(path, "[RealisticSmoke]\nIntensity=0.10\n");

        modguard::ConfigSlot slot;
        modguard::ConfigWatcher watcher(path, nullptr, slot);
        watcher.start();
        assert(watcher.running());

        const modguard::ExhaustConfig *initial = wait_for_publish(
            slot, [](const auto &c) { return intensity_in(0.05F, 0.15F, c); },
            std::chrono::seconds(3));
        assert(initial != nullptr);
        delete initial;

        // Unlink so the watcher consumes DELETE_SELF and enters the
        // vanish-rearm spin loop.
        ::unlink(path.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto t0 = std::chrono::steady_clock::now();
        watcher.stop();
        auto elapsed = std::chrono::steady_clock::now() - t0;
        assert(elapsed < std::chrono::seconds(2));
        assert(!watcher.running());
    }

    return 0;
}

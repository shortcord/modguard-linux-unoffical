#include "particle_hook.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

int protect_calls = 0;

bool always_protect(void *, int)
{
    return true;
}

// The first call makes the page writable. Every later call is a restore to
// read-execute and fails.
bool fail_restore_always(void *, int)
{
    ++protect_calls;
    return protect_calls != 2 && protect_calls != 3;
}

// The make-writable call fails. Nothing is written.
bool always_fail_first(void *, int)
{
    ++protect_calls;
    return false;
}

} // namespace

int main()
{
    std::uint8_t expected[16];
    std::uint8_t replacement[16];
    std::uint8_t buffer[16];
    std::memset(expected, 0xAA, sizeof(expected));
    std::memset(replacement, 0xBB, sizeof(replacement));

    // A successful patch writes the replacement bytes.
    std::memcpy(buffer, expected, sizeof(buffer));
    assert(modguard::apply_code_patch(buffer, expected, replacement, sizeof(buffer), always_protect));
    for (const auto byte : buffer) {
        assert(byte == 0xBB);
    }

    // A target-byte mismatch writes nothing and fails closed.
    std::uint8_t wrong[16];
    std::memset(wrong, 0xCC, sizeof(wrong));
    std::memcpy(buffer, expected, sizeof(buffer));
    assert(!modguard::apply_code_patch(buffer, wrong, replacement, sizeof(buffer), always_protect));
    for (const auto byte : buffer) {
        assert(byte == 0xAA);
    }

    // A missing protector fails closed.
    std::memcpy(buffer, expected, sizeof(buffer));
    assert(!modguard::apply_code_patch(buffer, expected, replacement, sizeof(buffer), nullptr));

    // If the read-execute restore fails, the patch is reverted while the page
    // is still writable. A false return means the target bytes are unchanged,
    // even when every restore attempt fails.
    std::memcpy(buffer, expected, sizeof(buffer));
    protect_calls = 0;
    assert(!modguard::apply_code_patch(buffer, expected, replacement, sizeof(buffer),
                                       fail_restore_always));
    for (const auto byte : buffer) {
        assert(byte == 0xAA);
    }
    assert(protect_calls == 3);

    // A make-writable failure leaves the target byte-identical and unpatched.
    std::memcpy(buffer, expected, sizeof(buffer));
    assert(!modguard::apply_code_patch(buffer, expected, replacement, sizeof(buffer),
                                       always_fail_first));
    for (const auto byte : buffer) {
        assert(byte == 0xAA);
    }

    return 0;
}

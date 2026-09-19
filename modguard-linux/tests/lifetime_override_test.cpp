// Tests for the lifetime-multiplier helper used by the particle hook.
//
// apply_lifetime_override scales the per-layer `time_to_live` field by the
// current multiplier. The helper must write `cached_original * multiplier`
// and be idempotent: successive calls with the same arguments produce the
// same value. A previous implementation read the current value back from the
// generator and re-multiplied, which compounded on every spawn and made
// particles effectively never die.

#include "particle_hook.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template <typename T>
void write(std::vector<std::uint8_t> &buffer, std::size_t offset, T value)
{
    assert(offset + sizeof(T) <= buffer.size());
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

template <typename T>
T read(const std::vector<std::uint8_t> &buffer, std::size_t offset)
{
    assert(offset + sizeof(T) <= buffer.size());
    T value{};
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

} // namespace

int main()
{
    // Idempotency: applying the same multiplier N times must converge to
    // `original * multiplier`, not `original * multiplier^N`.
    {
        std::vector<std::uint8_t> generator(0x700);
        constexpr std::size_t kLifetimeOffset = 0x62c;
        constexpr float kOriginal = 3.0F;
        constexpr float kMultiplier = 2.0F;
        write(generator, kLifetimeOffset, kOriginal);

        for (int i = 0; i < 100; ++i) {
            modguard::apply_lifetime_to_buffer(
                generator.data(), kLifetimeOffset, kOriginal, kMultiplier);
        }
        const float after = read<float>(generator, kLifetimeOffset);
        assert(std::fabs(after - kOriginal * kMultiplier) < 1e-5F);
    }

    // Multiplier = 1 leaves the value untouched.
    {
        std::vector<std::uint8_t> generator(0x700);
        constexpr std::size_t kLifetimeOffset = 0x62c;
        constexpr float kOriginal = 0.7F;
        write(generator, kLifetimeOffset, kOriginal);

        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, kOriginal, 1.0F);
        assert(std::fabs(read<float>(generator, kLifetimeOffset) - kOriginal) < 1e-5F);
    }

    // Multiplier changes: applying the new multiplier overwrites the previous
    // scaled value with `original * new_multiplier`.
    {
        std::vector<std::uint8_t> generator(0x700);
        constexpr std::size_t kLifetimeOffset = 0x62c;
        constexpr float kOriginal = 3.0F;
        write(generator, kLifetimeOffset, kOriginal);

        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, kOriginal, 2.0F);
        assert(std::fabs(read<float>(generator, kLifetimeOffset) - 6.0F) < 1e-5F);

        // Simulate a config reload: a new multiplier. The helper must use the
        // cached original, not the in-memory scaled value, so 3.0 * 5 = 15
        // rather than 6.0 * 5 = 30.
        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, kOriginal, 5.0F);
        assert(std::fabs(read<float>(generator, kLifetimeOffset) - 15.0F) < 1e-5F);
    }

    // Invalid cached values are no-ops.
    {
        std::vector<std::uint8_t> generator(0x700);
        constexpr std::size_t kLifetimeOffset = 0x62c;
        constexpr float kSentinel = 42.0F;
        write(generator, kLifetimeOffset, kSentinel);

        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, 0.0F, 2.0F);
        assert(read<float>(generator, kLifetimeOffset) == kSentinel);

        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, -1.0F, 2.0F);
        assert(read<float>(generator, kLifetimeOffset) == kSentinel);

        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, std::nanf(""), 2.0F);
        assert(read<float>(generator, kLifetimeOffset) == kSentinel);
    }

    // Config reload after lifetime has been corrupted: the helper resets the
    // in-memory value to `original * new_multiplier`, recovering from prior
    // compounding.
    {
        std::vector<std::uint8_t> generator(0x700);
        constexpr std::size_t kLifetimeOffset = 0x62c;
        constexpr float kOriginal = 2.0F;
        // Simulate prior compounding by writing a large value.
        constexpr float kCorrupted = 1.0e6F;
        write(generator, kLifetimeOffset, kCorrupted);

        // Apply with multiplier = 1 to recover the original behavior.
        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, kOriginal, 1.0F);
        assert(std::fabs(read<float>(generator, kLifetimeOffset) - kOriginal) < 1e-5F);

        // Now apply a real multiplier; it should be applied to the original,
        // not to the recovered value.
        modguard::apply_lifetime_to_buffer(
            generator.data(), kLifetimeOffset, kOriginal, 3.0F);
        assert(std::fabs(read<float>(generator, kLifetimeOffset) - 6.0F) < 1e-5F);
    }

    return 0;
}

#include "particle_signature.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace modguard {
namespace {

template <typename T>
T read(const std::uint8_t *address)
{
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

bool near(float value, float expected)
{
    return std::isfinite(value) && std::fabs(value - expected) <= 0.0005F;
}

bool is_smoke_layer(std::int32_t count, float size_x, float size_y, float lifetime)
{
    return (count == 40 && near(size_x, 0.090F) && near(size_y, 0.090F) && near(lifetime, 3.0F)) ||
           (count == 20 && near(size_x, 0.040F) && near(size_y, 0.040F) && near(lifetime, 0.7F)) ||
           (count == 4 && near(size_x, 0.020F) && near(size_y, 0.020F) && near(lifetime, 0.2F));
}

} // namespace

bool is_ets2_155_smoke_container(const void *container_pointer, std::uintptr_t image_base)
{
    if (!container_pointer) {
        return false;
    }
    const auto *container = static_cast<const std::uint8_t *>(container_pointer);
    const auto *generator = container - 0x548;
    if (read<std::uintptr_t>(generator) != image_base + 0x2c99180 ||
        read<std::uintptr_t>(container + 0x10) != image_base + 0x2c99150) {
        return false;
    }

    const std::int32_t count = read<std::int32_t>(generator + 0x70);
    const float size_x = read<float>(generator + 0x54);
    const float size_y = read<float>(generator + 0x58);
    const float lifetime = read<float>(generator + 0x524);
    return is_smoke_layer(count, size_x, size_y, lifetime);
}

bool is_ats_155_smoke_container(const void *container_pointer, std::uintptr_t image_base)
{
    if (!container_pointer) {
        return false;
    }
    const auto *container = static_cast<const std::uint8_t *>(container_pointer);
    const auto *generator = container - 0x640;
    if (read<std::uintptr_t>(generator) != image_base + 0x2d15040 ||
        read<std::uintptr_t>(container) != image_base + 0x2d15010) {
        return false;
    }

    return is_smoke_layer(read<std::int32_t>(generator + 0x80),
                          read<float>(generator + 0x60),
                          read<float>(generator + 0x64),
                          read<float>(generator + 0x7c));
}

bool is_ats_160_smoke_generator(const void *generator_pointer, std::uintptr_t image_base)
{
    if (!generator_pointer) {
        return false;
    }
    const auto *generator = static_cast<const std::uint8_t *>(generator_pointer);
    if (read<std::uintptr_t>(generator) != image_base + 0x2d15040) {
        return false;
    }

    return is_smoke_layer(read<std::int32_t>(generator + 0x630),
                          read<float>(generator + 0x58),
                          read<float>(generator + 0x5c),
                          read<float>(generator + 0x62c));
}

} // namespace modguard

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

int smoke_layer(std::int32_t count, float size_x, float size_y, float lifetime)
{
    if (count == 40 && near(size_x, 0.090F) && near(size_y, 0.090F) && near(lifetime, 3.0F)) {
        return 0;
    }
    if (count == 20 && near(size_x, 0.040F) && near(size_y, 0.040F) && near(lifetime, 0.7F)) {
        return 1;
    }
    if (count == 4 && near(size_x, 0.020F) && near(size_y, 0.020F) && near(lifetime, 0.2F)) {
        return 2;
    }
    return -1;
}

} // namespace

int classify_ets2_155_smoke_container(const void *container_pointer, std::uintptr_t image_base)
{
    if (!container_pointer) {
        return -1;
    }
    const auto *container = static_cast<const std::uint8_t *>(container_pointer);
    const auto *generator = container - 0x548;
    if (read<std::uintptr_t>(generator) != image_base + 0x2c99180 ||
        read<std::uintptr_t>(container + 0x10) != image_base + 0x2c99150) {
        return -1;
    }

    return smoke_layer(read<std::int32_t>(generator + 0x70),
                       read<float>(generator + 0x54),
                       read<float>(generator + 0x58),
                       read<float>(generator + 0x524));
}

int classify_ets2_160_smoke_generator(const void *generator_pointer, std::uintptr_t image_base)
{
    if (!generator_pointer) {
        return -1;
    }
    const auto *generator = static_cast<const std::uint8_t *>(generator_pointer);
    if (read<std::uintptr_t>(generator) != image_base + 0x2d0f600) {
        return -1;
    }

    return smoke_layer(read<std::int32_t>(generator + 0x630),
                       read<float>(generator + 0x58),
                       read<float>(generator + 0x5c),
                       read<float>(generator + 0x62c));
}

int classify_ats_155_smoke_container(const void *container_pointer, std::uintptr_t image_base)
{
    if (!container_pointer) {
        return -1;
    }
    const auto *container = static_cast<const std::uint8_t *>(container_pointer);
    const auto *generator = container - 0x640;
    if (read<std::uintptr_t>(generator) != image_base + 0x2d15040 ||
        read<std::uintptr_t>(container) != image_base + 0x2d15010) {
        return -1;
    }

    return smoke_layer(read<std::int32_t>(generator + 0x80),
                       read<float>(generator + 0x60),
                       read<float>(generator + 0x64),
                       read<float>(generator + 0x7c));
}

int classify_ats_160_smoke_generator(const void *generator_pointer, std::uintptr_t image_base)
{
    if (!generator_pointer) {
        return -1;
    }
    const auto *generator = static_cast<const std::uint8_t *>(generator_pointer);
    if (read<std::uintptr_t>(generator) != image_base + 0x2d15040) {
        return -1;
    }

    return smoke_layer(read<std::int32_t>(generator + 0x630),
                       read<float>(generator + 0x58),
                       read<float>(generator + 0x5c),
                       read<float>(generator + 0x62c));
}

int classify_particle_gen_u_smoke(const void *generator_pointer,
                                  std::uintptr_t image_base,
                                  std::uintptr_t expected_vtable_offset)
{
    if (!generator_pointer) {
        return -1;
    }
    const auto *generator = static_cast<const std::uint8_t *>(generator_pointer);
    if (read<std::uintptr_t>(generator) != image_base + expected_vtable_offset) {
        return -1;
    }
    return smoke_layer(read<std::int32_t>(generator + 0x630),
                       read<float>(generator + 0x54),
                       read<float>(generator + 0x58),
                       read<float>(generator + 0x62c));
}

int classify_ats_161_smoke_generator(const void *generator_pointer, std::uintptr_t image_base)
{
    const auto inspection = inspect_ats_161_smoke_generator(generator_pointer, image_base);
    return inspection.base_vtable ? inspection.layer : -1;
}

int classify_ets2_161_smoke_generator(const void *generator_pointer, std::uintptr_t image_base)
{
    return classify_particle_gen_u_smoke(generator_pointer, image_base, 0x2e35488);
}

int classify_ats_162_smoke_generator(const void *generator_pointer, std::uintptr_t image_base)
{
    return classify_particle_gen_u_smoke(generator_pointer, image_base, 0x2e43508);
}

Ats161SmokeInspection inspect_ats_161_smoke_generator(
    const void *generator_pointer, std::uintptr_t image_base)
{
    if (!generator_pointer) {
        return {};
    }
    const auto *generator = static_cast<const std::uint8_t *>(generator_pointer);
    const auto vtable = read<std::uintptr_t>(generator);
    const auto count = read<std::int32_t>(generator + 0x630);
    const auto size_x = read<float>(generator + 0x54);
    const auto size_y = read<float>(generator + 0x58);
    const auto lifetime = read<float>(generator + 0x62c);
    const int layer = smoke_layer(count, size_x, size_y, lifetime);
    return {
        vtable == image_base + 0x2e41fc8,
        vtable == image_base + 0x2e416f8,
        count == 40 || count == 20 || count == 4,
        (near(size_x, 0.090F) && near(size_y, 0.090F)) ||
            (near(size_x, 0.040F) && near(size_y, 0.040F)) ||
            (near(size_x, 0.020F) && near(size_y, 0.020F)),
        near(lifetime, 3.0F) || near(lifetime, 0.7F) || near(lifetime, 0.2F),
        layer,
    };
}

bool is_ets2_155_smoke_container(const void *container, std::uintptr_t image_base)
{
    return classify_ets2_155_smoke_container(container, image_base) >= 0;
}

bool is_ets2_160_smoke_generator(const void *generator, std::uintptr_t image_base)
{
    return classify_ets2_160_smoke_generator(generator, image_base) >= 0;
}

bool is_ats_155_smoke_container(const void *container, std::uintptr_t image_base)
{
    return classify_ats_155_smoke_container(container, image_base) >= 0;
}

bool is_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base)
{
    return classify_ats_160_smoke_generator(generator, image_base) >= 0;
}

bool is_ats_161_smoke_generator(const void *generator, std::uintptr_t image_base)
{
    return classify_ats_161_smoke_generator(generator, image_base) >= 0;
}

bool is_ets2_161_smoke_generator(const void *generator, std::uintptr_t image_base)
{
    return classify_ets2_161_smoke_generator(generator, image_base) >= 0;
}

bool is_ats_162_smoke_generator(const void *generator, std::uintptr_t image_base)
{
    return classify_ats_162_smoke_generator(generator, image_base) >= 0;
}

bool particle_build_id_matches(const std::uint8_t *actual, std::size_t actual_size,
                               const std::uint8_t *expected, std::size_t expected_size)
{
    return actual && expected && actual_size == expected_size && expected_size != 0 &&
           std::memcmp(actual, expected, expected_size) == 0;
}

std::size_t select_particle_profile(const ParticleProfileIdentity *profiles,
                                    std::size_t profile_count, const char *game_id,
                                    std::uint32_t game_version,
                                    const std::uint8_t *build_id,
                                    std::size_t build_id_size)
{
    if (!profiles || !game_id) {
        return no_particle_profile;
    }

    std::size_t fallback = no_particle_profile;
    for (std::size_t index = 0; index < profile_count; ++index) {
        const auto &candidate = profiles[index];
        if (!candidate.game_id || std::strcmp(game_id, candidate.game_id) != 0) {
            continue;
        }
        if (candidate.expected_build_id) {
            if (particle_build_id_matches(build_id, build_id_size,
                                          candidate.expected_build_id,
                                          candidate.expected_build_id_size)) {
                return index;
            }
        } else if (game_version >= candidate.min_version &&
                   game_version <= candidate.max_version &&
                   fallback == no_particle_profile) {
            fallback = index;
        }
    }
    return fallback;
}

} // namespace modguard

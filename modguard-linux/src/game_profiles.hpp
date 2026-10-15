#ifndef MODGUARD_GAME_PROFILES_HPP
#define MODGUARD_GAME_PROFILES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace modguard {

struct GameProfile {
    const char *name;
    const char *game_id;
    std::uint32_t telemetry_version;
    std::array<std::uint8_t, 20> expected_build_id;
    std::size_t expected_build_id_size;
    std::uintptr_t spawn_offset;
    std::uintptr_t constructor_offset;
    std::uintptr_t list_allocate_offset;
    std::array<std::uint8_t, 16> expected_spawn;
    std::size_t spawn_size;
    std::array<std::uint8_t, 18> expected_constructor;
    std::size_t constructor_size;
};

inline constexpr std::size_t game_profile_count = 8;

const std::array<GameProfile, game_profile_count> &game_profiles();

} // namespace modguard

#endif

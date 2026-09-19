#ifndef MODGUARD_PARTICLE_SIGNATURE_HPP
#define MODGUARD_PARTICLE_SIGNATURE_HPP

#include <cstddef>
#include <cstdint>

namespace modguard {

struct ParticleProfileIdentity {
    const char *game_id;
    std::uint32_t min_version;
    std::uint32_t max_version;
    const std::uint8_t *expected_build_id;
    std::size_t expected_build_id_size;
};

struct Ats161SmokeInspection {
    bool base_vtable = false;
    bool emitter_vtable = false;
    bool count_match = false;
    bool size_match = false;
    bool lifetime_match = false;
    int layer = -1;
};


constexpr std::size_t no_particle_profile = static_cast<std::size_t>(-1);

// Layer classifiers return the smoke layer index (0, 1, or 2) for the three
// documented generators, or -1 when the object is not a recognized smoke layer.
int classify_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
int classify_ets2_160_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
int classify_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_161_smoke_generator(const void *generator, std::uintptr_t image_base);
Ats161SmokeInspection inspect_ats_161_smoke_generator(
    const void *generator, std::uintptr_t image_base);

bool is_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ets2_160_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_161_smoke_generator(const void *generator, std::uintptr_t image_base);

bool particle_build_id_matches(const std::uint8_t *actual, std::size_t actual_size,
                               const std::uint8_t *expected, std::size_t expected_size);

std::size_t select_particle_profile(const ParticleProfileIdentity *profiles,
                                    std::size_t profile_count, const char *game_id,
                                    std::uint32_t game_version,
                                    const std::uint8_t *build_id,
                                    std::size_t build_id_size);

} // namespace modguard

#endif

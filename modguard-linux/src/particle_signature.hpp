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

// Smoke identity values returned by every classifier.
//
// -1 means "not a recognized smoke generator". The three legacy plume layers
// are 0, 1, and 2. The three dynamic-blend appearance bands are 3, 4, and 5.
// The dynamic bands all share one count and one lifetime so each generator
// requests particles at the same rate; only the base size differs. That equal
// request rate is what makes weighted admission blend without changing the
// total emission.
constexpr int particle_identity_none = -1;
constexpr int particle_identity_legacy_haze = 0;
constexpr int particle_identity_legacy_mid = 1;
constexpr int particle_identity_legacy_core = 2;
constexpr int particle_identity_dynamic_low = 3;
constexpr int particle_identity_dynamic_medium = 4;
constexpr int particle_identity_dynamic_high = 5;
constexpr int particle_identity_max = particle_identity_dynamic_high;

// Number of independent admission-sampler streams. One per identity value.
constexpr std::size_t particle_identity_stream_count = 6;

// Layer classifiers return a smoke identity value, or particle_identity_none
// when the object is not a recognized smoke generator.
int classify_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
int classify_ets2_160_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
int classify_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_161_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ets2_161_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_162_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_163_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_164_smoke_generator(const void *generator, std::uintptr_t image_base);
int classify_ats_165_smoke_generator(const void *generator, std::uintptr_t image_base);
Ats161SmokeInspection inspect_ats_161_smoke_generator(
    const void *generator, std::uintptr_t image_base);

// Shared 1.61 layout: particle_gen_u vtable at +0x0, count at +0x630,
// particle_size at +0x54/+0x58, time_to_live at +0x62c. Each caller supplies
// the build-specific vtable offset so the check stays pinned.
int classify_particle_gen_u_smoke(const void *generator, std::uintptr_t image_base,
                                  std::uintptr_t expected_vtable_offset);

bool is_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ets2_160_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_161_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ets2_161_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_162_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_163_smoke_generator(const void *generator, std::uintptr_t image_base);
bool is_ats_164_smoke_generator(const void *generator, std::uintptr_t image_base);

bool particle_build_id_matches(const std::uint8_t *actual, std::size_t actual_size,
                               const std::uint8_t *expected, std::size_t expected_size);

std::size_t select_particle_profile(const ParticleProfileIdentity *profiles,
                                    std::size_t profile_count, const char *game_id,
                                    std::uint32_t game_version,
                                    const std::uint8_t *build_id,
                                    std::size_t build_id_size);

} // namespace modguard

#endif

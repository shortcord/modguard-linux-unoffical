#include "particle_signature.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template <typename T>
void write(std::vector<std::uint8_t> &object, std::size_t offset, T value)
{
    std::memcpy(object.data() + offset, &value, sizeof(value));
}

void set_ets2_layer(std::vector<std::uint8_t> &generator, std::int32_t count, float size, float lifetime)
{
    write(generator, 0x70, count);
    write(generator, 0x54, size);
    write(generator, 0x58, size);
    write(generator, 0x524, lifetime);
}

void set_ats_layer(std::vector<std::uint8_t> &generator, std::int32_t count, float size, float lifetime)
{
    write(generator, 0x80, count);
    write(generator, 0x60, size);
    write(generator, 0x64, size);
    write(generator, 0x7c, lifetime);
}

void set_ats160_layer(std::vector<std::uint8_t> &generator, std::int32_t count, float size, float lifetime)
{
    write(generator, 0x630, count);
    write(generator, 0x58, size);
    write(generator, 0x5c, size);
    write(generator, 0x62c, lifetime);
}

void set_ats161_layer(std::vector<std::uint8_t> &generator, std::int32_t count, float size, float lifetime)
{
    write(generator, 0x630, count);
    write(generator, 0x54, size);
    write(generator, 0x58, size);
    write(generator, 0x62c, lifetime);
}

} // namespace

int main()
{
    constexpr std::uintptr_t base = 0x10000000;
    const std::uint8_t expected_build_id[] = {0x7f, 0xa4, 0xc9, 0xe7};
    std::uint8_t actual_build_id[] = {0x7f, 0xa4, 0xc9, 0xe7};
    assert(modguard::particle_build_id_matches(actual_build_id, sizeof(actual_build_id),
                                               expected_build_id, sizeof(expected_build_id)));
    actual_build_id[3] ^= 0xff;
    assert(!modguard::particle_build_id_matches(actual_build_id, sizeof(actual_build_id),
                                                expected_build_id, sizeof(expected_build_id)));
    assert(!modguard::particle_build_id_matches(expected_build_id, sizeof(expected_build_id) - 1,
                                                 expected_build_id, sizeof(expected_build_id)));

    const std::uint8_t pinned_build_id[] = {0xbb, 0xad, 0x4a, 0xec};
    const modguard::ParticleProfileIdentity profiles[] = {
        {"ats", 0x00010006, 0x00010006, nullptr, 0},
        {"ats", 0x00010006, 0x00010006, pinned_build_id, sizeof(pinned_build_id)},
        {"eut2", 0x00010013, 0x00010013, expected_build_id, sizeof(expected_build_id)},
    };
    assert(modguard::select_particle_profile(
               profiles, 3, "ats", 0x00010006, pinned_build_id,
               sizeof(pinned_build_id)) == 1);
    assert(modguard::select_particle_profile(
               profiles, 3, "ats", 0x00010006, expected_build_id,
               sizeof(expected_build_id)) == 0);
    assert(modguard::select_particle_profile(
               profiles, 3, "ats", 0x00010007, pinned_build_id,
               sizeof(pinned_build_id)) == 1);
    assert(modguard::select_particle_profile(
               profiles + 1, 1, "ats", 0x00010006, expected_build_id,
               sizeof(expected_build_id)) == modguard::no_particle_profile);
    assert(modguard::select_particle_profile(
               profiles, 1, "ats", 0x00010007, expected_build_id,
               sizeof(expected_build_id)) == modguard::no_particle_profile);
    assert(modguard::select_particle_profile(
               profiles, 3, "unsupported", 0x00010007, pinned_build_id,
               sizeof(pinned_build_id)) == modguard::no_particle_profile);
    assert(modguard::select_particle_profile(
               nullptr, 0, "ats", 0x00010006, pinned_build_id,
               sizeof(pinned_build_id)) == modguard::no_particle_profile);

    std::vector<std::uint8_t> ets2_generator(0x1968);
    write(ets2_generator, 0, base + 0x2c99180);
    write(ets2_generator, 0x558, base + 0x2c99150);
    void *ets2_container = ets2_generator.data() + 0x548;

    set_ets2_layer(ets2_generator, 40, 0.090F, 3.0F);
    assert(modguard::is_ets2_155_smoke_container(ets2_container, base));
    set_ets2_layer(ets2_generator, 20, 0.040F, 0.7F);
    assert(modguard::is_ets2_155_smoke_container(ets2_container, base));
    set_ets2_layer(ets2_generator, 4, 0.020F, 0.2F);
    assert(modguard::is_ets2_155_smoke_container(ets2_container, base));

    set_ets2_layer(ets2_generator, 60, 0.040F, 2.0F);
    assert(!modguard::is_ets2_155_smoke_container(ets2_container, base));
    write(ets2_generator, 0, base + 1);
    set_ets2_layer(ets2_generator, 40, 0.090F, 3.0F);
    assert(!modguard::is_ets2_155_smoke_container(ets2_container, base));
    write(ets2_generator, 0, base + 0x2c99180);
    write(ets2_generator, 0x558, base + 1);
    assert(!modguard::is_ets2_155_smoke_container(ets2_container, base));

    std::vector<std::uint8_t> ets2_160_generator(0x700);
    write(ets2_160_generator, 0, base + 0x2d0f600);
    set_ats160_layer(ets2_160_generator, 40, 0.090F, 3.0F);
    assert(modguard::is_ets2_160_smoke_generator(ets2_160_generator.data(), base));
    set_ats160_layer(ets2_160_generator, 20, 0.040F, 0.7F);
    assert(modguard::is_ets2_160_smoke_generator(ets2_160_generator.data(), base));
    set_ats160_layer(ets2_160_generator, 4, 0.020F, 0.2F);
    assert(modguard::is_ets2_160_smoke_generator(ets2_160_generator.data(), base));
    set_ats160_layer(ets2_160_generator, 60, 0.040F, 2.0F);
    assert(!modguard::is_ets2_160_smoke_generator(ets2_160_generator.data(), base));
    write(ets2_160_generator, 0, base + 1);
    set_ats160_layer(ets2_160_generator, 40, 0.090F, 3.0F);
    assert(!modguard::is_ets2_160_smoke_generator(ets2_160_generator.data(), base));

    std::vector<std::uint8_t> ats_generator(0x1a68);
    write(ats_generator, 0, base + 0x2d15040);
    write(ats_generator, 0x640, base + 0x2d15010);
    void *ats_container = ats_generator.data() + 0x640;
    set_ats_layer(ats_generator, 40, 0.090F, 3.0F);
    assert(modguard::is_ats_155_smoke_container(ats_container, base));
    set_ats_layer(ats_generator, 20, 0.040F, 0.7F);
    assert(modguard::is_ats_155_smoke_container(ats_container, base));
    set_ats_layer(ats_generator, 4, 0.020F, 0.2F);
    assert(modguard::is_ats_155_smoke_container(ats_container, base));
    set_ats_layer(ats_generator, 4, 0.020F, 0.3F);
    assert(!modguard::is_ats_155_smoke_container(ats_container, base));
    set_ats_layer(ats_generator, 40, 0.090F, 3.0F);
    write(ats_generator, 0, base + 1);
    assert(!modguard::is_ats_155_smoke_container(ats_container, base));
    write(ats_generator, 0, base + 0x2d15040);
    write(ats_generator, 0x640, base + 1);
    assert(!modguard::is_ats_155_smoke_container(ats_container, base));

    std::vector<std::uint8_t> ats160_generator(0x700);
    write(ats160_generator, 0, base + 0x2d15040);
    set_ats160_layer(ats160_generator, 40, 0.090F, 3.0F);
    assert(modguard::is_ats_160_smoke_generator(ats160_generator.data(), base));
    set_ats160_layer(ats160_generator, 20, 0.040F, 0.7F);
    assert(modguard::is_ats_160_smoke_generator(ats160_generator.data(), base));
    set_ats160_layer(ats160_generator, 4, 0.020F, 0.2F);
    assert(modguard::is_ats_160_smoke_generator(ats160_generator.data(), base));
    set_ats160_layer(ats160_generator, 60, 0.040F, 2.0F);
    assert(!modguard::is_ats_160_smoke_generator(ats160_generator.data(), base));
    write(ats160_generator, 0, base + 1);
    set_ats160_layer(ats160_generator, 40, 0.090F, 3.0F);
    assert(!modguard::is_ats_160_smoke_generator(ats160_generator.data(), base));

    std::vector<std::uint8_t> ats161_generator(0x700);
    write(ats161_generator, 0, base + 0x2e41fc8);
    set_ats161_layer(ats161_generator, 40, 0.090F, 3.0F);
    auto inspection = modguard::inspect_ats_161_smoke_generator(ats161_generator.data(), base);
    assert(inspection.base_vtable);
    assert(!inspection.emitter_vtable);
    assert(inspection.count_match);
    assert(inspection.size_match);
    assert(inspection.lifetime_match);
    assert(inspection.layer == 0);
    assert(modguard::is_ats_161_smoke_generator(ats161_generator.data(), base));
    set_ats161_layer(ats161_generator, 20, 0.040F, 0.7F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) == 1);
    set_ats161_layer(ats161_generator, 4, 0.020F, 0.2F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) == 2);
    set_ats161_layer(ats161_generator, 60, 0.040F, 2.0F);
    assert(!modguard::is_ats_161_smoke_generator(ats161_generator.data(), base));
    write(ats161_generator, 0, base + 0x2e416f8);
    set_ats161_layer(ats161_generator, 40, 0.090F, 3.0F);
    inspection = modguard::inspect_ats_161_smoke_generator(ats161_generator.data(), base);
    assert(!inspection.base_vtable);
    assert(inspection.emitter_vtable);
    assert(inspection.layer == 0);
    assert(!modguard::is_ats_161_smoke_generator(ats161_generator.data(), base));
    write(ats161_generator, 0, base + 0x2e41fc8);
    set_ats161_layer(ats161_generator, 60, 0.090F, 3.0F);
    inspection = modguard::inspect_ats_161_smoke_generator(ats161_generator.data(), base);
    assert(!inspection.count_match);
    assert(inspection.size_match);
    assert(inspection.lifetime_match);
    assert(inspection.layer == -1);
    set_ats161_layer(ats161_generator, 40, 0.080F, 3.0F);
    inspection = modguard::inspect_ats_161_smoke_generator(ats161_generator.data(), base);
    assert(inspection.count_match);
    assert(!inspection.size_match);
    assert(inspection.lifetime_match);
    assert(inspection.layer == -1);
    set_ats161_layer(ats161_generator, 40, 0.090F, 2.0F);
    inspection = modguard::inspect_ats_161_smoke_generator(ats161_generator.data(), base);
    assert(inspection.count_match);
    assert(inspection.size_match);
    assert(!inspection.lifetime_match);
    assert(inspection.layer == -1);
    set_ats161_layer(ats161_generator, 40, 0.040F, 0.7F);
    inspection = modguard::inspect_ats_161_smoke_generator(ats161_generator.data(), base);
    assert(inspection.count_match);
    assert(inspection.size_match);
    assert(inspection.lifetime_match);
    assert(inspection.layer == -1);
    // The obsolete +0x58/+0x5c pair must not classify. +0x5c is
    // particle_size_var, not the size, so the pair is asymmetric.
    set_ats161_layer(ats161_generator, 40, 0.5F, 3.0F);
    write(ats161_generator, 0x58, 0.090F);
    write(ats161_generator, 0x5c, 0.090F);
    assert(!modguard::is_ats_161_smoke_generator(ats161_generator.data(), base));
    write(ats161_generator, 0, base + 1);
    assert(!modguard::is_ats_161_smoke_generator(ats161_generator.data(), base));

    // ETS2 1.61.1.1s and ATS 1.61.2.0s share the 1.61 particle_gen_u layout
    // (count +0x630, size +0x54/+0x58, lifetime +0x62c) but use build-specific
    // vtable address points.
    const std::uintptr_t ets2161_vtable = base + 0x2e35488;
    const std::uintptr_t ats162_vtable = base + 0x2e43508;
    std::vector<std::uint8_t> gen161(0x700);

    write(gen161, 0, ets2161_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(modguard::is_ets2_161_smoke_generator(gen161.data(), base));
    set_ats161_layer(gen161, 20, 0.040F, 0.7F);
    assert(modguard::classify_ets2_161_smoke_generator(gen161.data(), base) == 1);
    set_ats161_layer(gen161, 4, 0.020F, 0.2F);
    assert(modguard::classify_ets2_161_smoke_generator(gen161.data(), base) == 2);
    set_ats161_layer(gen161, 60, 0.040F, 2.0F);
    assert(!modguard::is_ets2_161_smoke_generator(gen161.data(), base));
    // The ATS 1.61.2.0 vtable must not satisfy the ETS2 classifier.
    write(gen161, 0, ats162_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(!modguard::is_ets2_161_smoke_generator(gen161.data(), base));
    write(gen161, 0, base + 1);
    assert(!modguard::is_ets2_161_smoke_generator(gen161.data(), base));

    write(gen161, 0, ats162_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(modguard::is_ats_162_smoke_generator(gen161.data(), base));
    set_ats161_layer(gen161, 20, 0.040F, 0.7F);
    assert(modguard::classify_ats_162_smoke_generator(gen161.data(), base) == 1);
    set_ats161_layer(gen161, 4, 0.020F, 0.2F);
    assert(modguard::classify_ats_162_smoke_generator(gen161.data(), base) == 2);
    set_ats161_layer(gen161, 60, 0.040F, 2.0F);
    assert(!modguard::is_ats_162_smoke_generator(gen161.data(), base));
    // The obsolete +0x58/+0x5c pair must not classify for the 1.61 layout.
    set_ats161_layer(gen161, 40, 0.5F, 3.0F);
    write(gen161, 0x58, 0.090F);
    write(gen161, 0x5c, 0.090F);
    assert(!modguard::is_ats_162_smoke_generator(gen161.data(), base));
    // The ETS2 1.61.1.1s vtable must not satisfy the ATS 1.61.2.0 classifier.
    write(gen161, 0, ets2161_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(!modguard::is_ats_162_smoke_generator(gen161.data(), base));
    write(gen161, 0, base + 1);
    assert(!modguard::is_ats_162_smoke_generator(gen161.data(), base));

    // ATS 1.61.3.0s uses the same 1.61 particle_gen_u layout as ATS 1.61.2.0s
    // but a build-specific vtable address point.
    const std::uintptr_t ats163_vtable = base + 0x2e43868;
    write(gen161, 0, ats163_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(modguard::is_ats_163_smoke_generator(gen161.data(), base));
    set_ats161_layer(gen161, 20, 0.040F, 0.7F);
    assert(modguard::classify_ats_163_smoke_generator(gen161.data(), base) == 1);
    set_ats161_layer(gen161, 4, 0.020F, 0.2F);
    assert(modguard::classify_ats_163_smoke_generator(gen161.data(), base) == 2);
    set_ats161_layer(gen161, 60, 0.040F, 2.0F);
    assert(!modguard::is_ats_163_smoke_generator(gen161.data(), base));
    // The obsolete +0x58/+0x5c pair must not classify for the 1.61 layout.
    set_ats161_layer(gen161, 40, 0.5F, 3.0F);
    write(gen161, 0x58, 0.090F);
    write(gen161, 0x5c, 0.090F);
    assert(!modguard::is_ats_163_smoke_generator(gen161.data(), base));
    // The ATS 1.61.2.0 and ETS2 1.61.1.1s vtables must not satisfy the
    // ATS 1.61.3.0 classifier.
    write(gen161, 0, ats162_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(!modguard::is_ats_163_smoke_generator(gen161.data(), base));
    write(gen161, 0, ets2161_vtable);
    assert(!modguard::is_ats_163_smoke_generator(gen161.data(), base));
    write(gen161, 0, base + 1);
    assert(!modguard::is_ats_163_smoke_generator(gen161.data(), base));

    // The shared helper selects the layer for each documented vtable pair.
    write(gen161, 0, base + 0x2e35488);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(modguard::classify_particle_gen_u_smoke(gen161.data(), base, 0x2e35488) == 0);
    assert(modguard::classify_particle_gen_u_smoke(gen161.data(), base, 0x2e43508) == -1);

    // ATS 1.61.3.1s uses the same 1.61 particle_gen_u layout as ATS 1.61.3.0s
    // but a build-specific vtable address point.
    const std::uintptr_t ats164_vtable = base + 0x2e43a48;
    write(gen161, 0, ats164_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(modguard::is_ats_164_smoke_generator(gen161.data(), base));
    set_ats161_layer(gen161, 20, 0.040F, 0.7F);
    assert(modguard::classify_ats_164_smoke_generator(gen161.data(), base) == 1);
    set_ats161_layer(gen161, 4, 0.020F, 0.2F);
    assert(modguard::classify_ats_164_smoke_generator(gen161.data(), base) == 2);
    set_ats161_layer(gen161, 60, 0.040F, 2.0F);
    assert(!modguard::is_ats_164_smoke_generator(gen161.data(), base));
    // The obsolete +0x58/+0x5c pair must not classify for the 1.61 layout.
    set_ats161_layer(gen161, 40, 0.5F, 3.0F);
    write(gen161, 0x58, 0.090F);
    write(gen161, 0x5c, 0.090F);
    assert(!modguard::is_ats_164_smoke_generator(gen161.data(), base));
    // The ATS 1.61.2.0, ATS 1.61.3.0, and ETS2 1.61.1.1s vtables must not
    // satisfy the ATS 1.61.3.1 classifier.
    write(gen161, 0, ats162_vtable);
    set_ats161_layer(gen161, 40, 0.090F, 3.0F);
    assert(!modguard::is_ats_164_smoke_generator(gen161.data(), base));
    write(gen161, 0, ats163_vtable);
    assert(!modguard::is_ats_164_smoke_generator(gen161.data(), base));
    write(gen161, 0, ets2161_vtable);
    assert(!modguard::is_ats_164_smoke_generator(gen161.data(), base));
    write(gen161, 0, base + 1);
    assert(!modguard::is_ats_164_smoke_generator(gen161.data(), base));
    // The dynamic bands must classify through the new vtable too.
    write(gen161, 0, ats164_vtable);
    set_ats161_layer(gen161, 24, 0.030F, 3.0F);
    assert(modguard::classify_ats_164_smoke_generator(gen161.data(), base) ==
           modguard::particle_identity_dynamic_low);

    // Dynamic-blend bands. All three share count 24 and lifetime 3.0; only the
    // base size differs. They must classify to distinct identities and must
    // not collide with the legacy layers.
    write(ats161_generator, 0, base + 0x2e41fc8);
    set_ats161_layer(ats161_generator, 24, 0.030F, 3.0F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_dynamic_low);
    set_ats161_layer(ats161_generator, 24, 0.060F, 3.0F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_dynamic_medium);
    set_ats161_layer(ats161_generator, 24, 0.120F, 3.0F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_dynamic_high);
    // Wrong count or lifetime for a dynamic size must not classify.
    set_ats161_layer(ats161_generator, 40, 0.060F, 3.0F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_none);
    set_ats161_layer(ats161_generator, 24, 0.060F, 0.7F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_none);
    // A size between two bands stays unrecognized; the tolerance is tight.
    set_ats161_layer(ats161_generator, 24, 0.045F, 3.0F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_none);

    // The legacy three-layer tuple 40/0.090/3.0 must stay a legacy haze, not a
    // dynamic band, despite sharing lifetime 3.0.
    set_ats161_layer(ats161_generator, 40, 0.090F, 3.0F);
    assert(modguard::classify_ats_161_smoke_generator(ats161_generator.data(), base) ==
           modguard::particle_identity_legacy_haze);

    return 0;
}

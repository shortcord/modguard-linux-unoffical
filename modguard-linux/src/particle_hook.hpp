#ifndef MODGUARD_PARTICLE_HOOK_HPP
#define MODGUARD_PARTICLE_HOOK_HPP

#include <cstddef>
#include <cstdint>

namespace modguard {

struct ParticleHookDiagnostics {
    std::uint64_t particle_constructors = 0;
    std::uint64_t particle_candidates = 0;
    std::uint64_t particle_registered = 0;
    std::uint64_t smoke_requests = 0;
    std::uint64_t admitted_requests = 0;
    std::uint64_t suppressed_requests = 0;
    // Requests/admissions for each dynamic-blend band (low, medium, high).
    // These are subsets of smoke_requests and admitted_requests.
    std::uint64_t dynamic_low_requests = 0;
    std::uint64_t dynamic_low_admitted = 0;
    std::uint64_t dynamic_medium_requests = 0;
    std::uint64_t dynamic_medium_admitted = 0;
    std::uint64_t dynamic_high_requests = 0;
    std::uint64_t dynamic_high_admitted = 0;
};

bool install_particle_hook(const char *game_id, std::uint32_t game_version);
void remove_particle_hook();
void set_particle_demand(float demand, bool eligible);
void particle_hook_frame_start();
bool particle_hook_active();
// Returns cumulative counters for the current hook installation.
ParticleHookDiagnostics particle_hook_diagnostics();
void set_particle_lifetime_multiplier(float multiplier);

// Writes `cached_original * multiplier` to the lifetime field at the given
// offset in `generator`. Idempotent: the cached original ensures successive
// calls do not compound on the value written by the previous call. Exposed
// for unit testing the lifetime-override contract against synthetic layouts.
void apply_lifetime_to_buffer(void *generator, std::size_t lifetime_offset,
                              float cached_original, float multiplier);

// Sets the page protection of `address`. `protection` uses the platform
// PROT_* values. Returns true when the operation succeeds.
using CodeProtector = bool (*)(void *address, int protection);

// Checks `target` against `expected`, writes `replacement`, and restores
// read-execute protection. If protection restore fails, it reverts the bytes
// and returns false. A false return always means `target` is unchanged and the
// caller must treat the patch as not installed. Exposed for unit testing.
bool apply_code_patch(std::uint8_t *target, const std::uint8_t *expected,
                      const std::uint8_t *replacement, std::size_t size,
                      CodeProtector protector);

} // namespace modguard

#endif

#ifndef MODGUARD_PARTICLE_HOOK_HPP
#define MODGUARD_PARTICLE_HOOK_HPP

#include <cstdint>

namespace modguard {

struct ParticleHookStats {
    std::uint64_t smoke_requests = 0;
    std::uint64_t admitted_requests = 0;
};

// Diagnostic counters surfaced for logging / debugging.
struct ParticleHookDiagnostics {
    std::uint64_t allocator_calls = 0;       // total hook invocations
    std::uint64_t registered_generators = 0;  // successful constructor registrations
    std::uint64_t registered_container_hits = 0;  // containers matching registration
    std::uint64_t smoke_container_hits = 0;  // containers matching signature
    std::uint64_t eligible_admits = 0;       // allocations admitted while eligible
    std::uint64_t eligible_suppressions = 0; // allocations suppressed while eligible
    std::uint64_t ineligible_passthroughs = 0;
};

bool install_particle_hook(const char *game_id, std::uint32_t game_version);
void remove_particle_hook();
void set_particle_demand(float demand, bool eligible);
void particle_hook_frame_start();
bool particle_hook_active();

} // namespace modguard

#endif

#ifndef MODGUARD_PARTICLE_SIGNATURE_HPP
#define MODGUARD_PARTICLE_SIGNATURE_HPP

#include <cstdint>

namespace modguard {

// Layer classifiers return the smoke layer index (0, 1, or 2) for the three
// documented generators, or -1 when the object is not a recognized smoke layer.
int classify_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
int classify_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
int classify_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);

bool is_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);

} // namespace modguard

#endif

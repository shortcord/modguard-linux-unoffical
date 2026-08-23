#ifndef MODGUARD_PARTICLE_SIGNATURE_HPP
#define MODGUARD_PARTICLE_SIGNATURE_HPP

#include <cstdint>

namespace modguard {

bool is_ets2_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ats_155_smoke_container(const void *container, std::uintptr_t image_base);
bool is_ats_160_smoke_generator(const void *generator, std::uintptr_t image_base);

} // namespace modguard

#endif

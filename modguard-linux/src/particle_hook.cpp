#include "particle_hook.hpp"

#include "particle_admission.hpp"
#include "particle_signature.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

namespace modguard {
namespace {

constexpr std::size_t allocator_patch_size = 16;
constexpr std::size_t spawn_patch_size_160 = 13;
constexpr std::size_t maximum_patch_size = 32;
constexpr std::size_t generator_capacity = 16384;

using ListAllocate = void *(*)(void *, std::size_t);
using GeneratorConstructor = void (*)(void *, void *);
using SpawnFunction = void (*)(void *);

enum class HookModel {
    allocator_trampoline,
    spawn_function_entry,
};

const std::array<std::uint8_t, allocator_patch_size> ets2_expected_allocator = {
    0x48, 0x83, 0xc7, 0x20, 0xbe, 0x58, 0x00, 0x00,
    0x00, 0xe9, 0x32, 0x94, 0xc2, 0xff, 0x66, 0x90
};
const std::array<std::uint8_t, allocator_patch_size> ats_expected_allocator = {
    0x48, 0x83, 0xc7, 0x20, 0xbe, 0x60, 0x00, 0x00,
    0x00, 0xe9, 0x82, 0x04, 0xc2, 0xff, 0x66, 0x90
};
const std::array<std::uint8_t, 16> ets2_expected_constructor = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x55, 0x41, 0x54,
    0x53, 0x48, 0x89, 0xfb, 0x48, 0x83, 0xec, 0x08
};
const std::array<std::uint8_t, 18> ats_expected_constructor = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x55, 0x41, 0xbd,
    0x00, 0x01, 0x00, 0x00, 0x41, 0x54, 0x53, 0x48,
    0x89, 0xfb
};
const std::array<std::uint8_t, 16> ats_160_expected_spawn = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x49, 0x89, 0xfe, 0x41, 0x55, 0x41, 0x54, 0x53
};

struct HookProfile {
    const char *game_id;
    std::uint32_t min_version;
    std::uint32_t max_version;
    HookModel model;
    std::uintptr_t spawn_offset;
    std::uintptr_t list_allocate_offset;
    std::uintptr_t constructor_offset;
    std::size_t container_offset;
    std::size_t node_size;
    const std::uint8_t *expected_spawn;
    std::size_t spawn_patch_size;
    const std::uint8_t *expected_constructor;
    std::size_t constructor_patch_size;
    int (*classify_smoke)(const void *, std::uintptr_t);
};

const HookProfile profiles[] = {
    {"ats", 0x00010006, 0x00010006, HookModel::spawn_function_entry,
     0x1fadac0, 0x1bd32b0, 0x1fb23c0, 0x640, 0x60,
     ats_160_expected_spawn.data(), spawn_patch_size_160,
     ats_expected_constructor.data(), ats_expected_constructor.size(),
     classify_ats_160_smoke_generator},
    {"ats", 0x00010005, 0x00010005, HookModel::allocator_trampoline,
     0x1fb2e20, 0x1bd32b0, 0x1fb23c0, 0x640, 0x60,
     ats_expected_allocator.data(), allocator_patch_size,
     ats_expected_constructor.data(), ats_expected_constructor.size(),
     classify_ats_155_smoke_container},
    {"eut2", 0x00010005, 0x00010005, HookModel::allocator_trampoline,
     0x1f47fb0, 0x1b713f0, 0x1f47640, 0x548, 0x58,
     ets2_expected_allocator.data(), allocator_patch_size,
     ets2_expected_constructor.data(), ets2_expected_constructor.size(),
     classify_ets2_155_smoke_container},
};

std::atomic<float> demand{0.0F};
std::atomic<bool> eligible{false};
std::atomic<bool> active{false};
std::atomic<std::uint64_t> active_calls{0};
LayerSampler sampler;
std::array<std::atomic<void *>, generator_capacity> generators{};
std::uintptr_t image_base = 0;
std::uintptr_t executable_begin = 0;
std::uintptr_t executable_end = 0;
std::uint8_t *spawn_address = nullptr;
std::uint8_t *constructor_address = nullptr;
ListAllocate list_allocate = nullptr;
GeneratorConstructor original_constructor = nullptr;
SpawnFunction original_spawn = nullptr;
void *constructor_trampoline = nullptr;
void *spawn_trampoline = nullptr;
void *self_handle = nullptr;
const HookProfile *profile = nullptr;
bool spawn_hook_installed = false;
bool constructor_hook_installed = false;

struct CallGuard {
    CallGuard() { active_calls.fetch_add(1, std::memory_order_acquire); }
    ~CallGuard() { active_calls.fetch_sub(1, std::memory_order_release); }
};

int find_main_image(dl_phdr_info *info, std::size_t, void *context)
{
    if (!info->dlpi_name || info->dlpi_name[0] == '\0') {
        *static_cast<std::uintptr_t *>(context) = info->dlpi_addr;
        for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
            const auto &header = info->dlpi_phdr[index];
            if (header.p_type == PT_LOAD && (header.p_flags & PF_X) != 0) {
                executable_begin = info->dlpi_addr + header.p_vaddr;
                executable_end = executable_begin + header.p_memsz;
                break;
            }
        }
        return 1;
    }
    return 0;
}

bool protect_code(void *address, int protection)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return false;
    const auto page = reinterpret_cast<std::uintptr_t>(address) &
                      ~static_cast<std::uintptr_t>(page_size - 1);
    return mprotect(reinterpret_cast<void *>(page), static_cast<std::size_t>(page_size), protection) == 0;
}

std::array<std::uint8_t, maximum_patch_size> jump_patch(void *replacement, std::size_t size)
{
    std::array<std::uint8_t, maximum_patch_size> patch{};
    patch.fill(0x90);
    if (size < 12 || size > patch.size()) return {};
    patch[0] = 0x48;
    patch[1] = 0xb8;
    const auto address = reinterpret_cast<std::uintptr_t>(replacement);
    std::memcpy(patch.data() + 2, &address, sizeof(address));
    patch[10] = 0xff;
    patch[11] = 0xe0;
    return patch;
}

bool replace_code(std::uint8_t *target, const std::uint8_t *expected,
                   const std::uint8_t *replacement, std::size_t size)
{
    if (std::memcmp(target, expected, size) != 0 ||
        !protect_code(target, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return false;
    }
    std::memcpy(target, replacement, size);
    __builtin___clear_cache(reinterpret_cast<char *>(target),
                             reinterpret_cast<char *>(target + size));
    (void)protect_code(target, PROT_READ | PROT_EXEC);
    return true;
}

void register_generator(void *generator)
{
    std::size_t index = (reinterpret_cast<std::uintptr_t>(generator) >> 4U) &
                        (generator_capacity - 1U);
    for (std::size_t probe = 0; probe < generator_capacity; ++probe) {
        auto &slot = generators[index];
        void *current = slot.load(std::memory_order_acquire);
        if (current == generator) return;
        if (!current && slot.compare_exchange_strong(
                current, generator, std::memory_order_release, std::memory_order_relaxed)) {
            return;
        }
        index = (index + 1U) & (generator_capacity - 1U);
    }
}

bool is_registered_container(void *container)
{
    void *candidate = static_cast<std::uint8_t *>(container) - profile->container_offset;
    std::size_t index = (reinterpret_cast<std::uintptr_t>(candidate) >> 4U) &
                        (generator_capacity - 1U);
    for (std::size_t probe = 0; probe < generator_capacity; ++probe) {
        void *generator = generators[index].load(std::memory_order_acquire);
        if (!generator) return false;
        if (generator == candidate) return true;
        index = (index + 1U) & (generator_capacity - 1U);
    }
    return false;
}

bool is_registered_generator(void *generator)
{
    std::size_t index = (reinterpret_cast<std::uintptr_t>(generator) >> 4U) &
                        (generator_capacity - 1U);
    for (std::size_t probe = 0; probe < generator_capacity; ++probe) {
        void *registered = generators[index].load(std::memory_order_acquire);
        if (!registered) return false;
        if (registered == generator) return true;
        index = (index + 1U) & (generator_capacity - 1U);
    }
    return false;
}

void advance_frame()
{
    sampler.frame_start();
}

void *allocate_original(void *container)
{
    return list_allocate(static_cast<std::uint8_t *>(container) + 0x20, profile->node_size);
}

extern "C" void *particle_allocator_hook(void *container)
{
    CallGuard guard;
    const int layer = is_registered_container(container)
                          ? profile->classify_smoke(container, image_base)
                          : -1;
    if (layer < 0) {
        return allocate_original(container);
    }
    if (!eligible.load(std::memory_order_acquire)) {
        return nullptr;
    }
    const float current = std::clamp(demand.load(std::memory_order_relaxed), 0.0F, 1.0F);
    const std::uint32_t threshold = static_cast<std::uint32_t>(current * 16777216.0F);
    if (threshold == 0) return nullptr;
    if (threshold != 0x1000000U && sampler.sample(layer) >= threshold) {
        return nullptr;
    }
    return allocate_original(container);
}

extern "C" void particle_spawn_hook(void *generator)
{
    CallGuard guard;
    const int layer = is_registered_generator(generator)
                          ? profile->classify_smoke(generator, image_base)
                          : -1;
    if (layer < 0) {
        original_spawn(generator);
        return;
    }
    if (!eligible.load(std::memory_order_acquire)) {
        return;
    }
    const float current = std::clamp(demand.load(std::memory_order_relaxed), 0.0F, 1.0F);
    const std::uint32_t threshold = static_cast<std::uint32_t>(current * 16777216.0F);
    if (threshold == 0) return;
    if (threshold != 0x1000000U && sampler.sample(layer) >= threshold) {
        return;
    }
    original_spawn(generator);
}

extern "C" void particle_generator_constructor_hook(void *generator, void *source)
{
    CallGuard guard;
    original_constructor(generator, source);
    register_generator(generator);
}

} // namespace

bool install_particle_hook(const char *game_id, std::uint32_t game_version)
{
    if (!game_id || active.load(std::memory_order_acquire)) {
        return false;
    }
    profile = nullptr;
    for (const auto &candidate : profiles) {
        if (std::strcmp(game_id, candidate.game_id) == 0 &&
            game_version >= candidate.min_version && game_version <= candidate.max_version) {
            profile = &candidate;
            break;
        }
    }
    if (!profile) return false;

    image_base = 0;
    executable_begin = executable_end = 0;
    dl_iterate_phdr(find_main_image, &image_base);
    if (image_base + profile->spawn_offset < executable_begin ||
        image_base + profile->spawn_offset + profile->spawn_patch_size > executable_end ||
        image_base + profile->constructor_offset + profile->constructor_patch_size > executable_end) {
        return false;
    }
    spawn_address = reinterpret_cast<std::uint8_t *>(image_base + profile->spawn_offset);
    constructor_address = reinterpret_cast<std::uint8_t *>(image_base + profile->constructor_offset);
    if (std::memcmp(spawn_address, profile->expected_spawn, profile->spawn_patch_size) != 0 ||
        std::memcmp(constructor_address, profile->expected_constructor,
                    profile->constructor_patch_size) != 0) {
        return false;
    }

    list_allocate = reinterpret_cast<ListAllocate>(image_base + profile->list_allocate_offset);

    constructor_trampoline = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (constructor_trampoline == MAP_FAILED) {
        constructor_trampoline = nullptr;
        return false;
    }
    std::memcpy(constructor_trampoline, profile->expected_constructor,
                profile->constructor_patch_size);
    const auto ctor_continuation = jump_patch(
        constructor_address + profile->constructor_patch_size, maximum_patch_size);
    std::memcpy(static_cast<std::uint8_t *>(constructor_trampoline) + profile->constructor_patch_size,
                ctor_continuation.data(), maximum_patch_size);
    original_constructor = reinterpret_cast<GeneratorConstructor>(constructor_trampoline);

    if (profile->model == HookModel::spawn_function_entry) {
        spawn_trampoline = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (spawn_trampoline == MAP_FAILED) {
            spawn_trampoline = nullptr;
            munmap(constructor_trampoline, 4096);
            constructor_trampoline = nullptr;
            original_constructor = nullptr;
            return false;
        }
        std::memcpy(spawn_trampoline, profile->expected_spawn, profile->spawn_patch_size);
        const auto spawn_continuation = jump_patch(
            spawn_address + profile->spawn_patch_size, maximum_patch_size);
        std::memcpy(static_cast<std::uint8_t *>(spawn_trampoline) + profile->spawn_patch_size,
                    spawn_continuation.data(), maximum_patch_size);
        original_spawn = reinterpret_cast<SpawnFunction>(spawn_trampoline);
    }

    for (auto &slot : generators) slot.store(nullptr, std::memory_order_relaxed);
    spawn_hook_installed = false;
    constructor_hook_installed = false;
    advance_frame();

    const auto constructor_patch = jump_patch(
        reinterpret_cast<void *>(&particle_generator_constructor_hook), profile->constructor_patch_size);
    const auto spawn_patch = jump_patch(
        profile->model == HookModel::spawn_function_entry
            ? reinterpret_cast<void *>(&particle_spawn_hook)
            : reinterpret_cast<void *>(&particle_allocator_hook),
        profile->spawn_patch_size);
    Dl_info self_info{};
    if (dladdr(reinterpret_cast<void *>(&particle_spawn_hook), &self_info) != 0 && self_info.dli_fname) {
        self_handle = dlopen(self_info.dli_fname, RTLD_NOW | RTLD_NODELETE);
    }
    constructor_hook_installed = replace_code(
        constructor_address, profile->expected_constructor, constructor_patch.data(),
        profile->constructor_patch_size);
    if (constructor_hook_installed) {
        spawn_hook_installed = replace_code(
            spawn_address, profile->expected_spawn, spawn_patch.data(),
            profile->spawn_patch_size);
    }
    if (!constructor_hook_installed || !spawn_hook_installed) {
        if (spawn_hook_installed &&
            replace_code(spawn_address, spawn_patch.data(), profile->expected_spawn,
                         profile->spawn_patch_size)) {
            spawn_hook_installed = false;
        }
        if (constructor_hook_installed &&
            replace_code(constructor_address, constructor_patch.data(),
                         profile->expected_constructor, profile->constructor_patch_size)) {
            constructor_hook_installed = false;
        }
        if (spawn_hook_installed || constructor_hook_installed) {
            active.store(true, std::memory_order_release);
            return false;
        }
        while (active_calls.load(std::memory_order_acquire) != 0) std::this_thread::yield();
        if (spawn_trampoline) {
            munmap(spawn_trampoline, 4096);
            spawn_trampoline = nullptr;
            original_spawn = nullptr;
        }
        munmap(constructor_trampoline, 4096);
        constructor_trampoline = nullptr;
        original_constructor = nullptr;
        return false;
    }
    active.store(true, std::memory_order_release);
    return true;
}

void remove_particle_hook()
{
    if (!active.load(std::memory_order_acquire) || !spawn_address || !constructor_address) return;
    eligible.store(false, std::memory_order_release);
    const auto spawn_patch = jump_patch(
        profile->model == HookModel::spawn_function_entry
            ? reinterpret_cast<void *>(&particle_spawn_hook)
            : reinterpret_cast<void *>(&particle_allocator_hook),
        profile->spawn_patch_size);
    const auto constructor_patch = jump_patch(
        reinterpret_cast<void *>(&particle_generator_constructor_hook), profile->constructor_patch_size);
    if (spawn_hook_installed &&
        replace_code(spawn_address, spawn_patch.data(), profile->expected_spawn,
                     profile->spawn_patch_size)) {
        spawn_hook_installed = false;
    }
    if (constructor_hook_installed &&
        replace_code(constructor_address, constructor_patch.data(),
                     profile->expected_constructor, profile->constructor_patch_size)) {
        constructor_hook_installed = false;
    }
    if (spawn_hook_installed || constructor_hook_installed) return;

    active.store(false, std::memory_order_release);
    while (active_calls.load(std::memory_order_acquire) != 0) std::this_thread::yield();
    if (spawn_trampoline) {
        munmap(spawn_trampoline, 4096);
        spawn_trampoline = nullptr;
        original_spawn = nullptr;
    }
    if (constructor_trampoline) {
        munmap(constructor_trampoline, 4096);
        constructor_trampoline = nullptr;
    }
}

void set_particle_demand(float value, bool is_eligible)
{
    demand.store(std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F),
                 std::memory_order_relaxed);
    eligible.store(is_eligible, std::memory_order_release);
}

void particle_hook_frame_start()
{
    if (active.load(std::memory_order_acquire)) {
        advance_frame();
    }
}

bool particle_hook_active()
{
    return active.load(std::memory_order_acquire);
}

} // namespace modguard

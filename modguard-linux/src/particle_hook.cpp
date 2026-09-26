#include "particle_hook.hpp"

#include "particle_admission.hpp"
#include "game_profiles.hpp"
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

constexpr std::size_t maximum_patch_size = 32;
constexpr std::size_t generator_capacity = 16384;
constexpr std::size_t maximum_build_id_size = 64;

using ListAllocate = void *(*)(void *, std::size_t);
using GeneratorConstructor = void (*)(void *, void *);
using SpawnFunction = void (*)(void *);

enum class HookModel {
    allocator_trampoline,
    spawn_function_entry,
    list_allocate_entry,
};

struct HookProfile {
    HookModel model;
    std::uintptr_t spawn_offset;
    std::uintptr_t list_allocate_offset;
    std::uintptr_t constructor_offset;
    std::size_t container_offset;
    std::size_t node_size;
    std::size_t lifetime_offset;
    int (*classify_smoke)(const void *, std::uintptr_t);
};

const HookProfile profiles[] = {
    {HookModel::spawn_function_entry,
     0x1fa8ab0, 0x1bce2a0, 0x1fad3b0, 0x640, 0x60, 0x62c,
     classify_ets2_160_smoke_generator},
    {HookModel::spawn_function_entry,
     0x1fadac0, 0x1bd32b0, 0x1fb23c0, 0x640, 0x60, 0x62c,
     classify_ats_160_smoke_generator},
    {HookModel::list_allocate_entry,
     0x1cbfea0, 0x1cbfea0, 0x20aaec0, 0x668, 0x60, 0x62c,
     classify_ats_161_smoke_generator},
    {HookModel::list_allocate_entry,
     0x1cb3d40, 0x1cb3d40, 0x209ed60, 0x668, 0x60, 0x62c,
     classify_ets2_161_smoke_generator},
    {HookModel::list_allocate_entry,
     0x1cc1310, 0x1cc1310, 0x20ac330, 0x668, 0x60, 0x62c,
     classify_ats_162_smoke_generator},
    // The 1.55 allocator_trampoline profiles are disabled. The game reads
    // the node returned by the patched allocator without a NULL check in
    // list-unlink paths (ATS crash dumps: SIGSEGV at 0x48/0x30/0x60, PC in
    // the node-unlink function at VA 0x1c3e620). Suppression by returning
    // nullptr crashes the game. See docs/linux-particle-hook.md.
};
static_assert(std::size(profiles) == game_profile_count);

std::atomic<float> demand{0.0F};
std::atomic<bool> eligible{false};
std::atomic<bool> active{false};
std::atomic<std::uint64_t> active_calls{0};
std::atomic<float> lifetime_multiplier{1.0F};
std::atomic<std::uint64_t> particle_constructors{0};
std::atomic<std::uint64_t> particle_candidates{0};
std::atomic<std::uint64_t> particle_registered{0};
std::atomic<std::uint64_t> smoke_requests{0};
std::atomic<std::uint64_t> admitted_requests{0};
std::atomic<std::uint64_t> suppressed_requests{0};
LayerSampler sampler;
std::array<std::atomic<void *>, generator_capacity> generators{};
std::array<std::atomic<float>, generator_capacity> original_lifetimes{};
std::array<std::atomic<std::uint8_t>, generator_capacity> lifetime_captured{};
std::uintptr_t image_base = 0;
std::uintptr_t executable_begin = 0;
std::uintptr_t executable_end = 0;
std::array<std::uint8_t, maximum_build_id_size> main_build_id{};
std::size_t main_build_id_size = 0;
std::uint8_t *spawn_address = nullptr;
std::uint8_t *constructor_address = nullptr;
ListAllocate list_allocate = nullptr;
GeneratorConstructor original_constructor = nullptr;
SpawnFunction original_spawn = nullptr;
void *constructor_trampoline = nullptr;
void *spawn_trampoline = nullptr;
void *self_handle = nullptr;
const HookProfile *profile = nullptr;
const GameProfile *game_profile = nullptr;
bool spawn_hook_installed = false;
bool constructor_hook_installed = false;

void reset_diagnostics()
{
    particle_constructors.store(0, std::memory_order_relaxed);
    particle_candidates.store(0, std::memory_order_relaxed);
    particle_registered.store(0, std::memory_order_relaxed);
    smoke_requests.store(0, std::memory_order_relaxed);
    admitted_requests.store(0, std::memory_order_relaxed);
    suppressed_requests.store(0, std::memory_order_relaxed);
}

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
            } else if (header.p_type == PT_NOTE) {
                const auto *notes = reinterpret_cast<const std::uint8_t *>(
                    info->dlpi_addr + header.p_vaddr);
                const std::size_t notes_size = header.p_memsz;
                std::size_t offset = 0;
                while (offset <= notes_size && notes_size - offset >= sizeof(ElfW(Nhdr))) {
                    ElfW(Nhdr) note{};
                    std::memcpy(&note, notes + offset, sizeof(note));
                    offset += sizeof(note);
                    if (note.n_namesz > notes_size - offset || note.n_namesz > SIZE_MAX - 3U) break;
                    const auto *name = notes + offset;
                    const std::size_t name_size = (note.n_namesz + 3U) & ~std::size_t{3U};
                    if (name_size > notes_size - offset) break;
                    offset += name_size;
                    if (note.n_descsz > notes_size - offset || note.n_descsz > SIZE_MAX - 3U) break;
                    const auto *description = notes + offset;
                    const std::size_t description_size = (note.n_descsz + 3U) & ~std::size_t{3U};
                    if (description_size > notes_size - offset) break;
                    offset += description_size;
                    if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 &&
                        std::memcmp(name, "GNU\0", 4) == 0 &&
                        note.n_descsz <= main_build_id.size()) {
                        std::memcpy(main_build_id.data(), description, note.n_descsz);
                        main_build_id_size = note.n_descsz;
                    }
                }
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

bool apply_code_patch_impl(std::uint8_t *target, const std::uint8_t *expected,
                           const std::uint8_t *replacement, std::size_t size,
                           CodeProtector protector)
{
    if (!target || !expected || !replacement || !protector) {
        return false;
    }
    if (std::memcmp(target, expected, size) != 0) {
        return false;
    }
    if (!protector(target, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return false;
    }
    std::memcpy(target, replacement, size);
    __builtin___clear_cache(reinterpret_cast<char *>(target),
                             reinterpret_cast<char *>(target + size));
    if (protector(target, PROT_READ | PROT_EXEC)) {
        return true;
    }
    // Restore failed. A failed mprotect leaves the previous protection in
    // place, so the page is still writable. Revert the bytes now, while the
    // write is still possible. A false return always means the target bytes
    // are unchanged.
    std::memcpy(target, expected, size);
    __builtin___clear_cache(reinterpret_cast<char *>(target),
                             reinterpret_cast<char *>(target + size));
    (void)protector(target, PROT_READ | PROT_EXEC);
    return false;
}

bool replace_code(std::uint8_t *target, const std::uint8_t *expected,
                   const std::uint8_t *replacement, std::size_t size)
{
    return apply_code_patch_impl(target, expected, replacement, size, protect_code);
}

void register_generator(void *generator)
{
    std::size_t index = (reinterpret_cast<std::uintptr_t>(generator) >> 4U) &
                        (generator_capacity - 1U);
    // Capture the original time_to_live from the generator memory. Read it
    // before any override so subsequent apply_lifetime_override calls can
    // write original * multiplier without compounding across spawns.
    float original = 0.0F;
    if (profile) {
        auto *address = static_cast<std::uint8_t *>(generator) + profile->lifetime_offset;
        std::memcpy(&original, address, sizeof(original));
    }
    for (std::size_t probe = 0; probe < generator_capacity; ++probe) {
        auto &slot = generators[index];
        auto &cached_lifetime = original_lifetimes[index];
        auto &captured = lifetime_captured[index];
        void *current = slot.load(std::memory_order_acquire);
        if (current == generator) {
            // Re-register call: capture only if not yet recorded.
            std::uint8_t expected = 0;
            if (captured.compare_exchange_strong(expected, 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                cached_lifetime.store(original, std::memory_order_release);
            }
            return;
        }
        if (!current && slot.compare_exchange_strong(
                current, generator, std::memory_order_release, std::memory_order_relaxed)) {
            cached_lifetime.store(original, std::memory_order_release);
            captured.store(1, std::memory_order_release);
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

void apply_lifetime_override(void *generator)
{
    const float multiplier = lifetime_multiplier.load(std::memory_order_relaxed);
    if (!std::isfinite(multiplier) || multiplier == 1.0F || !profile) {
        return;
    }
    if (multiplier < 0.05F || multiplier > 60.0F) {
        return;
    }
    // Look up the original time_to_live captured at registration. Writing
    // original * multiplier each spawn would otherwise compound on top of the
    // value we wrote last spawn, causing runaway growth.
    std::size_t index = (reinterpret_cast<std::uintptr_t>(generator) >> 4U) &
                        (generator_capacity - 1U);
    float original = 0.0F;
    bool found = false;
    for (std::size_t probe = 0; probe < generator_capacity; ++probe) {
        void *slot_generator = generators[index].load(std::memory_order_acquire);
        if (!slot_generator) break;
        if (slot_generator == generator) {
            if (lifetime_captured[index].load(std::memory_order_acquire) != 0) {
                original = original_lifetimes[index].load(std::memory_order_acquire);
                found = true;
            }
            break;
        }
        index = (index + 1U) & (generator_capacity - 1U);
    }
    if (!found) {
        // Generator was not registered through the constructor hook; fall back
        // to a single-shot read so we still apply the override but do not
        // compound on subsequent spawns.
        auto *address = static_cast<std::uint8_t *>(generator) + profile->lifetime_offset;
        std::memcpy(&original, address, sizeof(original));
        if (!std::isfinite(original) || original <= 0.0F) {
            return;
        }
    } else if (!std::isfinite(original) || original <= 0.0F) {
        return;
    }
    apply_lifetime_to_buffer(generator, profile->lifetime_offset, original, multiplier);
}

void *allocate_original(void *container)
{
    return list_allocate(static_cast<std::uint8_t *>(container) + 0x20, profile->node_size);
}

extern "C" void *particle_allocator_hook(void *container)
{
    CallGuard guard;
    particle_candidates.fetch_add(1, std::memory_order_relaxed);
    const bool registered = is_registered_container(container);
    if (registered) {
        particle_registered.fetch_add(1, std::memory_order_relaxed);
    }
    const int layer = registered
                          ? profile->classify_smoke(container, image_base)
                          : -1;
    if (layer < 0) {
        return allocate_original(container);
    }
    smoke_requests.fetch_add(1, std::memory_order_relaxed);
    if (!eligible.load(std::memory_order_acquire)) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const float current = std::clamp(demand.load(std::memory_order_relaxed), 0.0F, 1.0F);
    const std::uint32_t threshold = static_cast<std::uint32_t>(current * 16777216.0F);
    if (threshold == 0) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    if (threshold != 0x1000000U && sampler.sample(layer) >= threshold) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    void *generator = static_cast<std::uint8_t *>(container) - profile->container_offset;
    apply_lifetime_override(generator);
    admitted_requests.fetch_add(1, std::memory_order_relaxed);
    return allocate_original(container);
}

extern "C" void *particle_list_allocate_hook(void *list_storage, std::size_t node_size)
{
    CallGuard guard;
    if (!list_storage || node_size != profile->node_size) {
        return list_allocate(list_storage, node_size);
    }
    const auto storage_address = reinterpret_cast<std::uintptr_t>(list_storage);
    if (storage_address < profile->container_offset) {
        return list_allocate(list_storage, node_size);
    }
    particle_candidates.fetch_add(1, std::memory_order_relaxed);
    void *generator = reinterpret_cast<void *>(storage_address - profile->container_offset);
    const bool registered = is_registered_generator(generator);
    if (registered) {
        particle_registered.fetch_add(1, std::memory_order_relaxed);
    }
    const int layer = registered ? profile->classify_smoke(generator, image_base) : -1;
    if (layer < 0) {
        return list_allocate(list_storage, node_size);
    }
    smoke_requests.fetch_add(1, std::memory_order_relaxed);
    if (!eligible.load(std::memory_order_acquire)) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const float current = std::clamp(demand.load(std::memory_order_relaxed), 0.0F, 1.0F);
    const std::uint32_t threshold = static_cast<std::uint32_t>(current * 16777216.0F);
    if (threshold == 0) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    if (threshold != 0x1000000U && sampler.sample(layer) >= threshold) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    apply_lifetime_override(generator);
    admitted_requests.fetch_add(1, std::memory_order_relaxed);
    return list_allocate(list_storage, node_size);
}

extern "C" void particle_spawn_hook(void *generator)
{
    CallGuard guard;
    particle_candidates.fetch_add(1, std::memory_order_relaxed);
    const bool registered = is_registered_generator(generator);
    if (registered) {
        particle_registered.fetch_add(1, std::memory_order_relaxed);
    }
    const int layer = registered
                          ? profile->classify_smoke(generator, image_base)
                          : -1;
    if (layer < 0) {
        original_spawn(generator);
        return;
    }
    smoke_requests.fetch_add(1, std::memory_order_relaxed);
    if (!eligible.load(std::memory_order_acquire)) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float current = std::clamp(demand.load(std::memory_order_relaxed), 0.0F, 1.0F);
    const std::uint32_t threshold = static_cast<std::uint32_t>(current * 16777216.0F);
    if (threshold == 0) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (threshold != 0x1000000U && sampler.sample(layer) >= threshold) {
        suppressed_requests.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    apply_lifetime_override(generator);
    admitted_requests.fetch_add(1, std::memory_order_relaxed);
    original_spawn(generator);
}

extern "C" void particle_generator_constructor_hook(void *generator, void *source)
{
    CallGuard guard;
    particle_constructors.fetch_add(1, std::memory_order_relaxed);
    original_constructor(generator, source);
    register_generator(generator);
}

} // namespace

bool apply_code_patch(std::uint8_t *target, const std::uint8_t *expected,
                      const std::uint8_t *replacement, std::size_t size,
                      CodeProtector protector)
{
    return apply_code_patch_impl(target, expected, replacement, size, protector);
}

void apply_lifetime_to_buffer(void *generator, std::size_t lifetime_offset,
                              float cached_original, float multiplier)
{
    if (!generator) return;
    if (!std::isfinite(cached_original) || cached_original <= 0.0F) return;
    if (!std::isfinite(multiplier)) return;
    auto *address = static_cast<std::uint8_t *>(generator) + lifetime_offset;
    const float scaled = cached_original * multiplier;
    std::memcpy(address, &scaled, sizeof(scaled));
}

bool install_particle_hook(const char *game_id, std::uint32_t game_version)
{
    if (!game_id || active.load(std::memory_order_acquire)) {
        return false;
    }
    reset_diagnostics();
    profile = nullptr;
    game_profile = nullptr;
    spawn_address = nullptr;
    constructor_address = nullptr;
    image_base = 0;
    executable_begin = executable_end = 0;
    main_build_id.fill(0);
    main_build_id_size = 0;
    dl_iterate_phdr(find_main_image, &image_base);
    const auto &profile_table = game_profiles();
    std::array<ParticleProfileIdentity, game_profile_count> profile_identities{};
    for (std::size_t index = 0; index < profile_table.size(); ++index) {
        profile_identities[index] = {
            profile_table[index].game_id, profile_table[index].telemetry_version,
            profile_table[index].telemetry_version,
            profile_table[index].expected_build_id_size == 0
                ? nullptr : profile_table[index].expected_build_id.data(),
            profile_table[index].expected_build_id_size};
    }
    const std::size_t profile_index = select_particle_profile(
        profile_identities.data(), profile_identities.size(), game_id, game_version,
        main_build_id.data(), main_build_id_size);
    if (profile_index == no_particle_profile || profile_index >= std::size(profiles)) {
        return false;
    }
    profile = &profiles[profile_index];
    game_profile = &profile_table[profile_index];
    const auto range_is_executable = [](std::uintptr_t offset, std::size_t size) {
        if (image_base > executable_begin || executable_begin > executable_end ||
            offset > executable_end - image_base) {
            return false;
        }
        const std::uintptr_t address = image_base + offset;
        return address >= executable_begin && size <= executable_end - address;
    };
    if (!range_is_executable(profile->spawn_offset, game_profile->spawn_size) ||
        !range_is_executable(profile->constructor_offset, game_profile->constructor_size) ||
        !range_is_executable(profile->list_allocate_offset, 1)) {
        return false;
    }
    spawn_address = reinterpret_cast<std::uint8_t *>(image_base + profile->spawn_offset);
    constructor_address = reinterpret_cast<std::uint8_t *>(image_base + profile->constructor_offset);
    if (std::memcmp(spawn_address, game_profile->expected_spawn.data(), game_profile->spawn_size) != 0 ||
        std::memcmp(constructor_address, game_profile->expected_constructor.data(),
                    game_profile->constructor_size) != 0) {
        return false;
    }

    list_allocate = reinterpret_cast<ListAllocate>(image_base + profile->list_allocate_offset);

    constructor_trampoline = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (constructor_trampoline == MAP_FAILED) {
        constructor_trampoline = nullptr;
        return false;
    }
    std::memcpy(constructor_trampoline, game_profile->expected_constructor.data(),
                game_profile->constructor_size);
    const auto ctor_continuation = jump_patch(
        constructor_address + game_profile->constructor_size, maximum_patch_size);
    std::memcpy(static_cast<std::uint8_t *>(constructor_trampoline) + game_profile->constructor_size,
                ctor_continuation.data(), maximum_patch_size);
    original_constructor = reinterpret_cast<GeneratorConstructor>(constructor_trampoline);

    if (profile->model == HookModel::spawn_function_entry ||
        profile->model == HookModel::list_allocate_entry) {
        spawn_trampoline = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (spawn_trampoline == MAP_FAILED) {
            spawn_trampoline = nullptr;
            munmap(constructor_trampoline, 4096);
            constructor_trampoline = nullptr;
            original_constructor = nullptr;
            return false;
        }
        std::memcpy(spawn_trampoline, game_profile->expected_spawn.data(), game_profile->spawn_size);
        const auto spawn_continuation = jump_patch(
            spawn_address + game_profile->spawn_size, maximum_patch_size);
        std::memcpy(static_cast<std::uint8_t *>(spawn_trampoline) + game_profile->spawn_size,
                    spawn_continuation.data(), maximum_patch_size);
        if (profile->model == HookModel::spawn_function_entry) {
            original_spawn = reinterpret_cast<SpawnFunction>(spawn_trampoline);
        } else {
            list_allocate = reinterpret_cast<ListAllocate>(spawn_trampoline);
        }
    }

    for (auto &slot : generators) slot.store(nullptr, std::memory_order_relaxed);
    for (auto &slot : original_lifetimes) slot.store(0.0F, std::memory_order_relaxed);
    for (auto &slot : lifetime_captured) slot.store(0, std::memory_order_relaxed);
    spawn_hook_installed = false;
    constructor_hook_installed = false;
    advance_frame();

    const auto constructor_patch = jump_patch(
        reinterpret_cast<void *>(&particle_generator_constructor_hook), game_profile->constructor_size);
    const auto spawn_patch = jump_patch(
        profile->model == HookModel::spawn_function_entry
            ? reinterpret_cast<void *>(&particle_spawn_hook)
            : profile->model == HookModel::list_allocate_entry
                ? reinterpret_cast<void *>(&particle_list_allocate_hook)
                : reinterpret_cast<void *>(&particle_allocator_hook),
        game_profile->spawn_size);
    Dl_info self_info{};
    if (dladdr(reinterpret_cast<void *>(&particle_spawn_hook), &self_info) != 0 && self_info.dli_fname) {
        self_handle = dlopen(self_info.dli_fname, RTLD_NOW | RTLD_NODELETE);
    }
    constructor_hook_installed = replace_code(
        constructor_address, game_profile->expected_constructor.data(), constructor_patch.data(),
        game_profile->constructor_size);
    if (constructor_hook_installed) {
        spawn_hook_installed = replace_code(
            spawn_address, game_profile->expected_spawn.data(), spawn_patch.data(),
            game_profile->spawn_size);
    }
    if (!constructor_hook_installed || !spawn_hook_installed) {
        if (spawn_hook_installed &&
            replace_code(spawn_address, spawn_patch.data(), game_profile->expected_spawn.data(),
                         game_profile->spawn_size)) {
            spawn_hook_installed = false;
        }
        if (constructor_hook_installed &&
            replace_code(constructor_address, constructor_patch.data(),
                          game_profile->expected_constructor.data(), game_profile->constructor_size)) {
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
        reset_diagnostics();
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
            : profile->model == HookModel::list_allocate_entry
                ? reinterpret_cast<void *>(&particle_list_allocate_hook)
                : reinterpret_cast<void *>(&particle_allocator_hook),
        game_profile->spawn_size);
    const auto constructor_patch = jump_patch(
        reinterpret_cast<void *>(&particle_generator_constructor_hook), game_profile->constructor_size);
    if (spawn_hook_installed &&
        replace_code(spawn_address, spawn_patch.data(), game_profile->expected_spawn.data(),
                     game_profile->spawn_size)) {
        spawn_hook_installed = false;
    }
    if (constructor_hook_installed &&
        replace_code(constructor_address, constructor_patch.data(),
                     game_profile->expected_constructor.data(), game_profile->constructor_size)) {
        constructor_hook_installed = false;
    }
    if (spawn_hook_installed || constructor_hook_installed) return;

    active.store(false, std::memory_order_release);
    while (active_calls.load(std::memory_order_acquire) != 0) std::this_thread::yield();
    if (spawn_trampoline) {
        munmap(spawn_trampoline, 4096);
        spawn_trampoline = nullptr;
        original_spawn = nullptr;
        list_allocate = nullptr;
    }
    if (constructor_trampoline) {
        munmap(constructor_trampoline, 4096);
        constructor_trampoline = nullptr;
    }
    reset_diagnostics();
}

void set_particle_demand(float value, bool is_eligible)
{
    demand.store(std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F),
                 std::memory_order_relaxed);
    eligible.store(is_eligible, std::memory_order_release);
}

void set_particle_lifetime_multiplier(float multiplier)
{
    const float sanitized = std::isfinite(multiplier)
        ? std::clamp(multiplier, 0.0F, 60.0F)
        : 1.0F;
    lifetime_multiplier.store(sanitized, std::memory_order_relaxed);
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

ParticleHookDiagnostics particle_hook_diagnostics()
{
    // Relaxed atomics can produce a mixed-time snapshot. Clamp child stages to
    // their observed parents so published diagnostics preserve stage bounds.
    const auto candidates = particle_candidates.load(std::memory_order_relaxed);
    const auto registered = std::min(
        particle_registered.load(std::memory_order_relaxed), candidates);
    const auto smoke = std::min(
        smoke_requests.load(std::memory_order_relaxed), registered);
    const auto admitted = std::min(
        admitted_requests.load(std::memory_order_relaxed), smoke);
    const auto suppressed = std::min(
        suppressed_requests.load(std::memory_order_relaxed), smoke);
    return {
        particle_constructors.load(std::memory_order_relaxed),
        candidates,
        registered,
        smoke,
        admitted,
        suppressed,
    };
}

} // namespace modguard

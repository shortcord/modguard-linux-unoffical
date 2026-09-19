#include "game_detector.hpp"

#include "game_profiles.hpp"

#include <elf.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace modguard {
namespace {

constexpr std::size_t maximum_build_id_size = 64;

bool range_in_file(std::size_t offset, std::size_t size, std::size_t file_size)
{
    return offset <= file_size && size <= file_size - offset;
}

bool range_in_segment(std::uintptr_t address, std::size_t size, const Elf64_Phdr &segment)
{
    if (address < segment.p_vaddr) return false;
    const auto offset = address - segment.p_vaddr;
    return offset <= segment.p_filesz && size <= segment.p_filesz - offset;
}

bool matches(const std::vector<std::uint8_t> &file, const Elf64_Phdr &segment,
             std::uintptr_t address, const std::uint8_t *expected, std::size_t size)
{
    if (!range_in_segment(address, size, segment)) return false;
    const auto file_offset = segment.p_offset + (address - segment.p_vaddr);
    if (file_offset > file.size() || size > file.size() - file_offset) return false;
    return std::memcmp(file.data() + file_offset, expected, size) == 0;
}

} // namespace

DetectionResult detect_game_binary(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {DetectionStatus::unreadable, {}, {}, "cannot open file"};
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)), {});
    if (file.size() < sizeof(Elf64_Ehdr)) {
        return {DetectionStatus::unsupported, {}, {}, "file is not a supported ELF64 executable"};
    }

    Elf64_Ehdr header{};
    std::memcpy(&header, file.data(), sizeof(header));
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_machine != EM_X86_64 || header.e_phentsize != sizeof(Elf64_Phdr) ||
        !range_in_file(header.e_phoff, static_cast<std::size_t>(header.e_phnum) * sizeof(Elf64_Phdr),
                       file.size())) {
        return {DetectionStatus::unsupported, {}, {}, "file is not a supported ELF64 executable"};
    }

    std::vector<Elf64_Phdr> segments(header.e_phnum);
    std::memcpy(segments.data(), file.data() + header.e_phoff,
                segments.size() * sizeof(Elf64_Phdr));
    const Elf64_Phdr *executable = nullptr;
    std::array<std::uint8_t, maximum_build_id_size> build_id{};
    std::size_t build_id_size = 0;
    for (const auto &segment : segments) {
        if (segment.p_type == PT_LOAD && (segment.p_flags & PF_X) != 0 && !executable) {
            executable = &segment;
        }
        if (segment.p_type != PT_NOTE || !range_in_file(segment.p_offset, segment.p_filesz, file.size())) {
            continue;
        }
        std::size_t offset = 0;
        while (offset + sizeof(Elf64_Nhdr) <= segment.p_filesz) {
            Elf64_Nhdr note{};
            std::memcpy(&note, file.data() + segment.p_offset + offset, sizeof(note));
            offset += sizeof(note);
            const std::size_t name_size = (note.n_namesz + 3U) & ~std::size_t{3U};
            const std::size_t desc_size = (note.n_descsz + 3U) & ~std::size_t{3U};
            if (name_size > segment.p_filesz - offset ||
                desc_size > segment.p_filesz - offset - name_size) break;
            const auto *name = file.data() + segment.p_offset + offset;
            offset += name_size;
            const auto *description = file.data() + segment.p_offset + offset;
            offset += desc_size;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 &&
                std::memcmp(name, "GNU\0", 4) == 0 && note.n_descsz <= build_id.size()) {
                std::memcpy(build_id.data(), description, note.n_descsz);
                build_id_size = note.n_descsz;
            }
        }
    }
    if (!executable) return {DetectionStatus::unsupported, {}, {}, "no executable segment"};

    for (const auto &profile : game_profiles()) {
        const bool build_matches = profile.expected_build_id_size == 0 ||
            (build_id_size == profile.expected_build_id_size &&
             std::memcmp(build_id.data(), profile.expected_build_id.data(), build_id_size) == 0);
        if (!build_matches ||
            !matches(file, *executable, profile.spawn_offset, profile.expected_spawn.data(), profile.spawn_size) ||
            !matches(file, *executable, profile.constructor_offset,
                     profile.expected_constructor.data(), profile.constructor_size)) {
            continue;
        }
        return {DetectionStatus::supported, profile.name, profile.game_id, {}};
    }
    return {DetectionStatus::unsupported, {}, {}, "build ID or machine-code signature does not match"};
}

} // namespace modguard

#include "game_detector.hpp"
#include "game_profiles.hpp"

#include <cassert>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <elf.h>

namespace {

std::filesystem::path write_fixture(const modguard::GameProfile &profile,
                                     const std::vector<std::uint8_t> &build_id,
                                     bool corrupt_code)
{
    constexpr std::size_t image_size = 0x2100000;
    constexpr std::size_t code_offset = 0;
    std::vector<std::uint8_t> image(image_size, 0);

    Elf64_Ehdr header{};
    header.e_ident[EI_MAG0] = ELFMAG0;
    header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2;
    header.e_ident[EI_MAG3] = ELFMAG3;
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_type = ET_EXEC;
    header.e_machine = EM_X86_64;
    header.e_version = EV_CURRENT;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = 2;

    Elf64_Phdr code{};
    code.p_type = PT_LOAD;
    code.p_flags = PF_R | PF_X;
    code.p_offset = code_offset;
    code.p_vaddr = code_offset;
    code.p_filesz = image_size - code_offset;
    code.p_memsz = code.p_filesz;

    const std::size_t note_offset = 0x2000;
    Elf64_Phdr note{};
    note.p_type = PT_NOTE;
    note.p_offset = note_offset;
    note.p_filesz = 4 + 4 + 4 + 4 + build_id.size();
    note.p_memsz = note.p_filesz;

    std::memcpy(image.data(), &header, sizeof(header));
    std::memcpy(image.data() + header.e_phoff, &code, sizeof(code));
    std::memcpy(image.data() + header.e_phoff + sizeof(code), &note, sizeof(note));

    const std::uint32_t namesz = 4;
    const std::uint32_t descsz = static_cast<std::uint32_t>(build_id.size());
    const std::uint32_t type = NT_GNU_BUILD_ID;
    std::memcpy(image.data() + note_offset, &namesz, sizeof(namesz));
    std::memcpy(image.data() + note_offset + 4, &descsz, sizeof(descsz));
    std::memcpy(image.data() + note_offset + 8, &type, sizeof(type));
    std::memcpy(image.data() + note_offset + 12, "GNU\0", 4);
    std::memcpy(image.data() + note_offset + 16, build_id.data(), build_id.size());

    std::memcpy(image.data() + profile.spawn_offset, profile.expected_spawn.data(),
                profile.expected_spawn.size());
    std::memcpy(image.data() + profile.constructor_offset, profile.expected_constructor.data(),
                profile.expected_constructor.size());
    if (corrupt_code) image[profile.spawn_offset] ^= 0xff;

    const auto path = std::filesystem::temp_directory_path() / "modguard-game-detector-test.elf";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    return path;
}

} // namespace

int main()
{
    const auto &profiles = modguard::game_profiles();
    assert(!profiles.empty());

    for (const auto &profile : profiles) {
        // Every built-in profile is pinned. An unpinned profile would accept
        // any build ID, which the detector must not do.
        assert(profile.expected_build_id_size != 0);
        const std::vector<std::uint8_t> build_id(profile.expected_build_id.begin(),
                                                 profile.expected_build_id.begin() +
                                                     profile.expected_build_id_size);

        const auto supported_path = write_fixture(profile, build_id, false);
        const auto supported = modguard::detect_game_binary(supported_path);
        assert(supported.status == modguard::DetectionStatus::supported);
        assert(supported.profile_name == profile.name);

        const auto mismatch_path = write_fixture(profile, build_id, true);
        const auto mismatch = modguard::detect_game_binary(mismatch_path);
        assert(mismatch.status == modguard::DetectionStatus::unsupported);

        std::filesystem::remove(supported_path);
        std::filesystem::remove(mismatch_path);

        // A wrong build ID must not select the profile even when the machine
        // code matches. This proves the profile is pinned, not a fallback.
        std::vector<std::uint8_t> wrong_build_id = build_id;
        wrong_build_id.front() ^= 0xff;
        const auto wrong_build_path = write_fixture(profile, wrong_build_id, false);
        const auto wrong_build = modguard::detect_game_binary(wrong_build_path);
        assert(wrong_build.status == modguard::DetectionStatus::unsupported);
        std::filesystem::remove(wrong_build_path);
    }

    return 0;
}

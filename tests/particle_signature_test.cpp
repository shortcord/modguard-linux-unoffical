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

} // namespace

int main()
{
    constexpr std::uintptr_t base = 0x10000000;
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
    return 0;
}

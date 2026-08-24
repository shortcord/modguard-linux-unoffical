#ifndef MODGUARD_CONFIG_HPP
#define MODGUARD_CONFIG_HPP

#include "exhaust_model.hpp"

#include <string>

namespace modguard {

struct LoadedConfig {
    ExhaustConfig exhaust;
    std::string path;
    bool found = false;
};

LoadedConfig load_config(const char *game_id);
LoadedConfig load_config_file(const std::string &path);

} // namespace modguard

#endif

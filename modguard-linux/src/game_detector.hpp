#ifndef MODGUARD_GAME_DETECTOR_HPP
#define MODGUARD_GAME_DETECTOR_HPP

#include <filesystem>
#include <string>

namespace modguard {

enum class DetectionStatus {
    supported,
    unsupported,
    unreadable,
};

struct DetectionResult {
    DetectionStatus status;
    std::string profile_name;
    std::string game_id;
    std::string detail;
};

DetectionResult detect_game_binary(const std::filesystem::path &path);

} // namespace modguard

#endif

#include "game_detector.hpp"

#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: modguard_check GAME_BINARY\n";
        return 2;
    }

    const auto result = modguard::detect_game_binary(argv[1]);
    if (result.status == modguard::DetectionStatus::supported) {
        std::cout << "supported: " << result.profile_name << " (" << result.game_id << ")\n";
        return 0;
    }
    std::cerr << (result.status == modguard::DetectionStatus::unreadable
                      ? "error: " : "unsupported: ")
              << result.detail << '\n';
    return result.status == modguard::DetectionStatus::unreadable ? 2 : 1;
}

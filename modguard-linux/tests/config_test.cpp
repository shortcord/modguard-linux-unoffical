#include "config.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <string>

#include <unistd.h>

int main()
{
    const std::string path = "/tmp/modguard-config-test-" + std::to_string(getpid()) + ".ini";
    {
        std::ofstream output(path);
        output << "[Ignored]\nIntensity=99\n"
               << "[RealisticSmoke]\n"
               << "SmokeHook=off\n"
               << "ContinuousSmoke=yes\n"
               << "Intensity=0.75 ; comment\n"
               << "ThrottleCurve=999\n"
               << "AttackSmoothness=-2\n"
               << "SessionPeakPercent=2\n"
               << "TruckMassKg=-5\n"
               << "TrailerMassKg=999999\n"
               << "LoadCurvePower=0\n"
               << "GrossWeightReferenceKg=0\n"
               << "LoadBaseline=2\n"
               << "ThrottleGatePower=0\n"
               << "RpmLeanFraction=2\n"
               << "RpmLeanPower=0\n"
               << "LoadBoostMax=-1\n"
               << "RpmWeight=not-a-number\n";
    }

    const auto loaded = modguard::load_config_file(path);
    assert(loaded.found);
    assert(loaded.path == path);
    assert(!loaded.exhaust.smoke_hook);
    assert(loaded.exhaust.continuous_smoke);
    assert(std::fabs(loaded.exhaust.intensity - 0.75F) < 0.0001F);
    assert(loaded.exhaust.throttle_curve == 100);
    assert(loaded.exhaust.attack_smoothness == 0.0F);
    assert(loaded.exhaust.session_peak_percent == 1.0F);
    assert(loaded.exhaust.truck_mass_kg == 0.0F);
    assert(loaded.exhaust.trailer_mass_kg == 50000.0F);
    assert(loaded.exhaust.load_curve_power == 0.1F);
    assert(loaded.exhaust.gross_weight_reference_kg == 1.0F);
    assert(loaded.exhaust.load_baseline == 1.0F);
    assert(loaded.exhaust.throttle_gate_power == 0.1F);
    assert(loaded.exhaust.rpm_lean_fraction == 1.0F);
    assert(loaded.exhaust.rpm_lean_power == 0.1F);
    assert(loaded.exhaust.load_boost_max == 0.0F);
    assert(loaded.exhaust.rpm_weight == 1.0F);

    const auto missing = modguard::load_config_file(path + ".missing");
    assert(!missing.found);
    assert(missing.exhaust.throttle_curve == 45);
    unlink(path.c_str());
    return 0;
}

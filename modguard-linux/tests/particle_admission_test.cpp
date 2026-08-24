#include "particle_admission.hpp"

#include <array>
#include <cassert>

int main()
{
    modguard::LayerSampler a;
    modguard::LayerSampler b;

    a.frame_start();
    b.frame_start();

    for (int layer = 0; layer < 3; ++layer) {
        for (int i = 0; i < 8; ++i) {
            assert(a.sample(layer) == b.sample(layer));
        }
    }

    for (int layer = 0; layer < 3; ++layer) {
        for (int i = 0; i < 8; ++i) {
            assert(a.sample(layer) != a.sample(layer + 1));
        }
    }

    assert(a.sample(0) != a.sample(0));
    return 0;
}

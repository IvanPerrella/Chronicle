#include <gtest/gtest.h>

#include <cmath>

#include "fishing.hpp"

namespace {

bool Finite(const float *v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

} // namespace

// A cast holds the float where it flies (FishLineSetUki): every pass of the step puts the line's
// float point and the float's own first point on that spot, so the link between them has no length
// on the next pass. The EE divides that zero by zero to the largest value; the host gives a NaN,
// which the float's other points keep once the hold ends and hand back to the line, the float the
// camera follows while casting, and the player after it.
TEST(FishingLine, AFloatHeldInFlightStaysFinite) {
    sceVu0FVECTOR rod = {10.0f, 20.0f, 30.0f, 1.0f};
    sceVu0FVECTOR flight = {40.0f, 10.0f, 60.0f, 1.0f};
    FishLineInit(rod);
    cpoly = nullptr;
    WaterLevel = -100.0f;
    GroundLevel = -1000.0f;
    UkiGroundLevel = HookGroundLevel = LineGroundLevel = GroundLevel;

    for (int frame = 0; frame < 3; frame++) {
        FishLineSetUki(flight, 1.0f);
        FishLineStep(rod, nullptr);
    }
    FishLineStep(rod, nullptr);

    for (int i = 0; i < 24; i++) {
        EXPECT_TRUE(Finite(point[i])) << "line point " << i;
    }
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(Finite(ukip[i])) << "float point " << i;
    }
    sceVu0FVECTOR uki_position;
    FishLineGetUki(uki_position);
    EXPECT_TRUE(Finite(uki_position));
}

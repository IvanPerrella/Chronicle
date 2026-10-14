#include <gtest/gtest.h>

#include <cmath>
#include <new>

#include "bound.hpp"
#include "cloth.hpp"

namespace {

float NormalizedRadius(const CBound &bound, const float *point) {
    float x = (point[0] - bound.position[0]) * bound.reciprocal[0];
    float y = (point[1] - bound.position[1]) * bound.reciprocal[1];
    float z = (point[2] - bound.position[2]) * bound.reciprocal[2];
    return std::sqrt(x * x + y * y + z * z);
}

} // namespace

TEST(ClothCollision, OverlappingBoundsDoNotKeepAveragedContactInside) {
    alignas(CCloth) unsigned char storage[sizeof(CCloth)] = {};
    CCloth *cloth = new (storage) CCloth(1, 2, 1.0f);
    CBound left(1.0f, 1.0f, 1.0f);
    CBound right(1.0f, 1.0f, 1.0f);
    left.position[0] = -0.5f;
    right.position[0] = 0.5f;
    left.next = &right;
    right.mask_bit = 1;

    cloth->bound = &left;
    cloth->stop = 1;
    cloth->floor_on = 0;
    cloth->gravity[1] = 0.0f;
    cloth->mask[0][1] = 3;
    cloth->point[0][1][0] = 0.0f;
    cloth->point[0][1][1] = 0.3f;
    cloth->point[0][1][2] = 0.0f;

    float left_contact[4], right_contact[4];
    ASSERT_NE(left.InCheck(cloth->point[0][1], left_contact), 0);
    ASSERT_NE(right.InCheck(cloth->point[0][1], right_contact), 0);
    float average[4] = {(left_contact[0] + right_contact[0]) * 0.5f,
                        (left_contact[1] + right_contact[1]) * 0.5f,
                        (left_contact[2] + right_contact[2]) * 0.5f, 1.0f};
    ASSERT_LT(NormalizedRadius(left, average), 0.8f);
    ASSERT_LT(NormalizedRadius(right, average), 0.8f);

    cloth->Step(0);
    EXPECT_GE(NormalizedRadius(left, cloth->point[0][1]), 0.999f);
    EXPECT_GE(NormalizedRadius(right, cloth->point[0][1]), 0.999f);
    cloth->~CCloth();
}

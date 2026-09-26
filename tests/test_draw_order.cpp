#include "kke/DrawOrder.h"

#include <gtest/gtest.h>

TEST(DrawOrder, NearestFirst) {
    const std::vector<glm::vec3> p = { { 0, 0, -10 }, { 0, 0, -2 }, { 5, 0, 0 }, { 0, 0, -30 } };
    const std::vector<uint32_t> order = kke::frontToBackOrder(p, glm::vec3(0.0f));
    EXPECT_EQ(order, (std::vector<uint32_t>{ 1, 2, 0, 3 }));
}

TEST(DrawOrder, TiesKeepInputOrder) {
    const std::vector<glm::vec3> p = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { -1, 0, 0 } };
    const std::vector<uint32_t> order = kke::frontToBackOrder(p, glm::vec3(0.0f));
    EXPECT_EQ(order, (std::vector<uint32_t>{ 0, 1, 2, 3 }));
}

TEST(DrawOrder, Empty) { EXPECT_TRUE(kke::frontToBackOrder({}, glm::vec3(1.0f)).empty()); }

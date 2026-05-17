// Moved verbatim from spc-data/tests/test_geometry.cpp (Workstream C).
// Asserts are identical; only the include path + namespace changed.

#include "spc/geometry.hpp"

#include <gtest/gtest.h>

namespace {

using namespace spc;

TEST(Geometry, Square) {
	// Unit square centered at (0,0).
	const Polygon square = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}, {-1.0, -1.0}};
	EXPECT_TRUE(point_in_polygon(0.0, 0.0, square));
	EXPECT_TRUE(point_in_polygon(0.5, 0.5, square));
	EXPECT_FALSE(point_in_polygon(1.5, 0.0, square));
	EXPECT_FALSE(point_in_polygon(-1.5, 0.0, square));
	EXPECT_FALSE(point_in_polygon(0.0, 1.5, square));
}

TEST(Geometry, TriangleOverOklahoma) {
	// Triangle approximately covering western OK (lon -103..-98, lat 34..37).
	const Polygon ok_tri = {{-103.0, 34.0}, {-98.0, 34.0}, {-100.5, 37.0}, {-103.0, 34.0}};
	EXPECT_TRUE(point_in_polygon(-100.0, 35.0, ok_tri));  // central OK
	EXPECT_FALSE(point_in_polygon(-97.0, 35.0, ok_tri));  // just east of triangle
	EXPECT_FALSE(point_in_polygon(-100.0, 32.0, ok_tri)); // too far south
}

TEST(Geometry, MultiRingFeature) {
	OutlookFeature feat;
	feat.rings.push_back({{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}, {0.0, 0.0}});
	feat.rings.push_back({{10.0, 10.0}, {11.0, 10.0}, {11.0, 11.0}, {10.0, 11.0}, {10.0, 10.0}});
	EXPECT_TRUE(point_in_feature(0.5, 0.5, feat));
	EXPECT_TRUE(point_in_feature(10.5, 10.5, feat));
	EXPECT_FALSE(point_in_feature(5.0, 5.0, feat));
}

} // namespace

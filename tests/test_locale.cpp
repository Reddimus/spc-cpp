/// @file test_locale.cpp
/// @brief SPC publishes probabilities as numeric strings ("0.15"). Decoding
/// them must not depend on the host application's LC_NUMERIC: a host that
/// calls setlocale(LC_ALL, "") on a comma-decimal desktop (as Qt/GTK apps do)
/// would otherwise get a successful but silently empty outlook.

#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/outlook.hpp"

#include <clocale>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace {

using namespace spc;

std::string slurp(const std::string& name) {
	std::ifstream f(std::filesystem::path(SPC_FIXTURES_DIR) / name, std::ios::binary);
	EXPECT_TRUE(f.is_open()) << "missing fixture: " << name;
	std::stringstream buf;
	buf << f.rdbuf();
	return buf.str();
}

/// setlocale is process-global, so restore it on every exit path.
class ScopedNumericLocale {
public:
	explicit ScopedNumericLocale(const char* name) {
		const char* previous = std::setlocale(LC_NUMERIC, nullptr);
		saved_ = previous != nullptr ? previous : "C";
		applied_ = std::setlocale(LC_NUMERIC, name) != nullptr;
	}
	~ScopedNumericLocale() { (void)std::setlocale(LC_NUMERIC, saved_.c_str()); }
	ScopedNumericLocale(const ScopedNumericLocale&) = delete;
	ScopedNumericLocale& operator=(const ScopedNumericLocale&) = delete;
	ScopedNumericLocale(ScopedNumericLocale&&) = delete;
	ScopedNumericLocale& operator=(ScopedNumericLocale&&) = delete;

	[[nodiscard]] bool applied() const noexcept { return applied_; }

private:
	std::string saved_;
	bool applied_{false};
};

TEST(LocaleIndependence, Day48StaticFeedKeepsItsProbabilityUnderACommaDecimalLocale) {
	// The live Day 4-8 feed carries exactly one probability field and it is a
	// string: {"DN": 15, "LABEL": "0.15"}. Under a comma-decimal locale
	// strtod("0.15") consumes only "0", so the feature was dropped by the
	// probability > 0.0 gate and the whole product came back empty.
	const ScopedNumericLocale locale{"de_DE.UTF-8"};
	if (!locale.applied()) {
		GTEST_SKIP() << "de_DE.UTF-8 is not installed on this host";
	}

	const Day48OutlookPayload payload = parse_day4_8(slurp("day4prob.nolyr.geojson"), 4);

	ASSERT_EQ(payload.features.size(), 1u);
	EXPECT_DOUBLE_EQ(payload.features[0].probability, 0.15);
}

TEST(LocaleIndependence, ProbabilisticOutlookKeepsItsIsoplethsUnderACommaDecimalLocale) {
	const ScopedNumericLocale locale{"de_DE.UTF-8"};
	if (!locale.applied()) {
		GTEST_SKIP() << "de_DE.UTF-8 is not installed on this host";
	}

	const ProbOutlookPayload payload =
		parse_probabilistic(slurp("arcgis_day1_prob_tornado.geojson"), 1, "tornado");

	ASSERT_GT(payload.features.size(), 0u);
	for (const ProbOutlookFeature& f : payload.features) {
		EXPECT_GE(f.probability, 0.01);
		EXPECT_LE(f.probability, 1.0);
	}
}

TEST(LocaleIndependence, FireWeatherNoRiskSentinelIsStillDroppedUnderACommaDecimalLocale) {
	// has_zero_dn requires the whole string to parse; a comma-decimal strtod
	// stops at the '.' of "0.0", the full-consume check fails, and the no-risk
	// sentinel polygon ships as a real band.
	const ScopedNumericLocale locale{"de_DE.UTF-8"};
	if (!locale.applied()) {
		GTEST_SKIP() << "de_DE.UTF-8 is not installed on this host";
	}
	const std::string body = R"({"features":[
		{"attributes":{"dn":"0.0"},"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}},
		{"attributes":{"dn":"5"},"geometry":{"rings":[[[2,1],[3,1],[3,0],[2,0],[2,1]]]}}
	]})";

	const FireWeatherPayload payload =
		parse_fire_weather(body, 1, FireWeatherLayer::DryThunderstorm);

	ASSERT_EQ(payload.features.size(), 1u);
	EXPECT_EQ(payload.features[0].label, "IDRT");
}

} // namespace

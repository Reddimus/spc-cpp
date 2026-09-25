/// @file test_locale.cpp
/// @brief Results must not depend on the host's C locale. Apps that call
/// setlocale(LC_ALL, "") (most Qt and GTK apps) get the user's locale, which
/// may use a decimal comma or treat bytes above 0x7F as letters.

#include "models/json.hpp"
#include "spc/api.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/outlook.hpp"
#include "support/fixtures.hpp"

#include <clocale>
#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace spc;
using test::read_fixture;

/// setlocale is process-global, so restore it on every exit path.
class ScopedLocale {
public:
	ScopedLocale(int category, const char* name) : category_(category) {
		const char* previous = std::setlocale(category_, nullptr);
		saved_ = previous != nullptr ? previous : "C";
		applied_ = std::setlocale(category_, name) != nullptr;
	}
	~ScopedLocale() { (void)std::setlocale(category_, saved_.c_str()); }
	ScopedLocale(const ScopedLocale&) = delete;
	ScopedLocale& operator=(const ScopedLocale&) = delete;
	ScopedLocale(ScopedLocale&&) = delete;
	ScopedLocale& operator=(ScopedLocale&&) = delete;

	[[nodiscard]] bool applied() const noexcept { return applied_; }

private:
	int category_;
	std::string saved_;
	bool applied_{false};
};

class RecordingTransport final : public HttpTransport {
public:
	mutable std::vector<std::string> requests;

	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const override {
		requests.emplace_back(path);
		return HttpResponse{200, R"({"features":[]})", {}};
	}
};

TEST(LocaleIndependence, NumericStringsParseLikeFromCharsInEveryLocale) {
	struct Case {
		std::string_view text;
		bool ok;
		double value;
		std::size_t consumed;
	};
	// std::from_chars results. "0x10" stops at the x: from_chars reads no hex,
	// though a stream would.
	const std::vector<Case> cases{
		{"0.15", true, 0.15, 4},  {"5", true, 5.0, 1},		 {".5", true, 0.5, 2},
		{"-.5", true, -0.5, 3},	  {"5.", true, 5.0, 2},		 {"1e3", true, 1000.0, 3},
		{"12abc", true, 12.0, 2}, {"0x10", true, 0.0, 1},	 {"+1", false, 0.0, 0},
		{" 1", false, 0.0, 0},	  {"", false, 0.0, 0},		 {"abc", false, 0.0, 0},
		{"1e400", false, 0.0, 0}, {"1e-400", false, 0.0, 0}, {"1e-310", true, 1e-310, 6},
		{"5e", true, 5.0, 1},
	};
	for (const char* name : {"C", "de_DE.UTF-8"}) {
		const ScopedLocale locale{LC_ALL, name};
		if (!locale.applied()) {
			continue;
		}
		for (const Case& c : cases) {
			const detail::ParsedNumber parsed = detail::parse_double(c.text);
			EXPECT_EQ(parsed.ok, c.ok) << name << ": " << c.text;
			if (c.ok) {
				EXPECT_DOUBLE_EQ(parsed.value, c.value) << name << ": " << c.text;
				EXPECT_EQ(parsed.consumed, c.consumed) << name << ": " << c.text;
			}
		}
	}
	const detail::ParsedNumber inf = detail::parse_double("inf");
	EXPECT_TRUE(inf.ok && std::isinf(inf.value) && inf.consumed == 3);
	const detail::ParsedNumber nan = detail::parse_double("nan");
	EXPECT_TRUE(nan.ok && std::isnan(nan.value) && nan.consumed == 3);
}

TEST(LocaleIndependence, Day48StaticFeedKeepsItsProbabilityUnderACommaDecimalLocale) {
	// The Day 4-8 feed's only probability is the string "0.15"; a
	// comma-decimal strtod reads it as 0 and the feature is dropped.
	const ScopedLocale locale{LC_NUMERIC, "de_DE.UTF-8"};
	if (!locale.applied()) {
		GTEST_SKIP() << "de_DE.UTF-8 is not installed on this host";
	}

	const Day48OutlookPayload payload = parse_day4_8(read_fixture("day4prob.nolyr.geojson"), 4);

	ASSERT_EQ(payload.features.size(), 1u);
	EXPECT_DOUBLE_EQ(payload.features[0].probability, 0.15);
}

TEST(LocaleIndependence, ProbabilisticOutlookKeepsItsIsoplethsUnderACommaDecimalLocale) {
	const ScopedLocale locale{LC_NUMERIC, "de_DE.UTF-8"};
	if (!locale.applied()) {
		GTEST_SKIP() << "de_DE.UTF-8 is not installed on this host";
	}

	const ProbOutlookPayload payload =
		parse_probabilistic(read_fixture("arcgis_day1_prob_tornado.geojson"), 1, "tornado");

	ASSERT_GT(payload.features.size(), 0u);
	for (const ProbOutlookFeature& f : payload.features) {
		EXPECT_GE(f.probability, 0.01);
		EXPECT_LE(f.probability, 1.0);
	}
}

TEST(LocaleIndependence, FireWeatherNoRiskSentinelIsStillDroppedUnderACommaDecimalLocale) {
	// A comma-decimal strtod stops at the '.' of "0.0", so the no-risk polygon
	// would ship as a real band.
	const ScopedLocale locale{LC_NUMERIC, "de_DE.UTF-8"};
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

TEST(LocaleIndependence, QueryValuesArePercentEncodedUnderAUtf8Locale) {
	// macOS's isalnum() accepts bytes above 0x7F in UTF-8 locales, which left
	// them unencoded in the URL.
	const ScopedLocale locale{LC_CTYPE, "de_DE.UTF-8"};
	if (!locale.applied()) {
		GTEST_SKIP() << "de_DE.UTF-8 is not installed on this host";
	}
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	const ArchiveClient client{transport};

	ASSERT_TRUE(client.storm_reports("2026-05-19T12:00Z", "2026-05-20T12:00Z", "\xC3\xA4"));

	ASSERT_EQ(transport->requests.size(), 1u);
	EXPECT_NE(transport->requests[0].find("&wfos=%C3%A4"), std::string::npos)
		<< transport->requests[0];
}

} // namespace

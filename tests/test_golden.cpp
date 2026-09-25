/// @file test_golden.cpp
/// @brief Each public parser's output on every fixture it reads, compared
/// byte for byte with tests/golden/<fixture>.txt, so a refactor cannot change
/// parsed output unnoticed.
///
/// After an intended change, rerun with SPC_UPDATE_GOLDEN=1 to rewrite the
/// files, then review the diff.

#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"
#include "support/fixtures.hpp"
#include "support/payload_text.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace spc;
using test::quote;
using test::read_fixture;
using test::to_text;

struct GoldenCase {
	const char* fixture;
	std::string (*render)(std::string_view body);
};

// Names the case in gtest's failure output.
void PrintTo(const GoldenCase& golden, std::ostream* out) {
	*out << golden.fixture;
}

const std::vector<GoldenCase> kCases = {
	{"day1otlk_cat.nolyr.geojson",
	 [](std::string_view body) { return to_text(parse_categorical(body, 1)); }},
	{"day2otlk_cat.nolyr.geojson",
	 [](std::string_view body) { return to_text(parse_categorical(body, 2)); }},
	{"day3otlk_cat.nolyr.geojson",
	 [](std::string_view body) { return to_text(parse_categorical(body, 3)); }},
	{"arcgis_day1_categorical.geojson",
	 [](std::string_view body) { return to_text(parse_categorical(body, 1)); }},
	{"arcgis_day2_categorical.geojson",
	 [](std::string_view body) { return to_text(parse_categorical(body, 2)); }},
	{"arcgis_day3_categorical.geojson",
	 [](std::string_view body) { return to_text(parse_categorical(body, 3)); }},
	{"arcgis_day1_prob_tornado.geojson",
	 [](std::string_view body) { return to_text(parse_probabilistic(body, 1, "tornado")); }},
	{"arcgis_day1_prob_hail.geojson",
	 [](std::string_view body) { return to_text(parse_probabilistic(body, 1, "hail")); }},
	{"arcgis_day1_prob_wind.geojson",
	 [](std::string_view body) { return to_text(parse_probabilistic(body, 1, "wind")); }},
	{"arcgis_day2_prob_wind.geojson",
	 [](std::string_view body) { return to_text(parse_probabilistic(body, 2, "wind")); }},
	{"arcgis_day1_torn_conditional_intensity.esri.json",
	 [](std::string_view body) {
		 return to_text(parse_conditional_intensity(body, 1, "tornado"));
	 }},
	{"day4prob.nolyr.geojson",
	 [](std::string_view body) { return to_text(parse_day4_8(body, 4)); }},
	{"arcgis_day4_8_nonempty.synthetic.json",
	 [](std::string_view body) { return to_text(parse_day4_8(body, 4)); }},
	{"arcgis_day1_fire_weather.esri.json",
	 [](std::string_view body) {
		 return to_text(parse_fire_weather(body, 1, FireWeatherLayer::Outlook));
	 }},
	{"arcgis_day2_fire_weather.esri.json",
	 [](std::string_view body) {
		 return to_text(parse_fire_weather(body, 2, FireWeatherLayer::Outlook));
	 }},
	{"arcgis_mesoscale_discussion.esri.json",
	 [](std::string_view body) { return to_text(parse_mesoscale_discussions(body)); }},
	{"arcgis_mesoscale_discussion_noarea.esri.json",
	 [](std::string_view body) { return to_text(parse_mesoscale_discussions(body)); }},
	{"iem_spc_watch.json", [](std::string_view body) { return to_text(parse_watches(body)); }},
	{"iem_storm_reports.json",
	 [](std::string_view body) { return to_text(parse_storm_reports(body)); }},
};

// Fixtures no public parser reads.
const std::vector<std::string_view> kNotParsed = {
	// Esri copies for the parity tests; parse_categorical and
	// parse_probabilistic read only GeoJSON.
	"arcgis_day1_categorical.esri.json",
	"arcgis_day2_categorical.esri.json",
	"arcgis_day3_categorical.esri.json",
	"arcgis_day1_prob_tornado.esri.json",
	"arcgis_day1_prob_hail.esri.json",
	"arcgis_day1_prob_wind.esri.json",
	"arcgis_day2_prob_wind.esri.json",
	// Layer metadata, and an HTML error page that every parser rejects.
	"arcgis_layers_2026-09-03.json",
	"spc_404_no_active_outlook.html",
};

std::filesystem::path golden_path(std::string_view fixture) {
	return std::filesystem::path(SPC_GOLDEN_DIR) / std::format("{}.txt", fixture);
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
	const std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool updating() {
	const char* flag = std::getenv("SPC_UPDATE_GOLDEN");
	return flag != nullptr && std::string_view{flag} == "1";
}

/// Up to 120 characters of `line` around `column`; ring lines run long.
std::string_view excerpt(std::string_view line, std::size_t column) {
	const std::size_t start = column > 40 ? column - 40 : 0;
	return line.substr(std::min(start, line.size()), 120);
}

/// Where `actual` first departs from `expected`, as a line, a column, and
/// both versions of that line, escaped so a stray \r or tab shows.
std::string first_difference(std::string_view expected, std::string_view actual) {
	std::size_t line = 1;
	std::size_t line_start = 0;
	std::size_t i = 0;
	while (i < expected.size() && i < actual.size() && expected[i] == actual[i]) {
		if (expected[i] == '\n') {
			++line;
			line_start = i + 1;
		}
		++i;
	}
	const std::string_view want = expected.substr(line_start, expected.find('\n', i) - line_start);
	const std::string_view got = actual.substr(line_start, actual.find('\n', i) - line_start);
	return std::format("first difference at line {}, column {}\n  expected: {}\n  actual:   {}",
					   line, i - line_start + 1, quote(excerpt(want, i - line_start)),
					   quote(excerpt(got, i - line_start)));
}

class Golden : public testing::TestWithParam<GoldenCase> {};

TEST_P(Golden, MatchesTheSavedOutput) {
	const GoldenCase& golden = GetParam();
	const std::string actual = golden.render(read_fixture(golden.fixture));
	const std::filesystem::path path = golden_path(golden.fixture);
	if (updating()) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file << actual;
		ASSERT_TRUE(file.good()) << "cannot write " << path;
		return;
	}
	const std::optional<std::string> expected = read_file(path);
	ASSERT_TRUE(expected.has_value()) << path << " is missing. Create it with SPC_UPDATE_GOLDEN=1.";
	EXPECT_TRUE(actual == *expected)
		<< first_difference(*expected, actual)
		<< "\nIf the change is intended, rerun with SPC_UPDATE_GOLDEN=1 and review the diff.";
}

std::string case_name(const testing::TestParamInfo<GoldenCase>& info) {
	std::string name = info.param.fixture;
	std::ranges::replace_if(
		name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) == 0; }, '_');
	return name;
}

INSTANTIATE_TEST_SUITE_P(Parsers, Golden, testing::ValuesIn(kCases), case_name);

bool has_case(std::string_view fixture) {
	return std::ranges::any_of(kCases,
							   [fixture](const GoldenCase& c) { return c.fixture == fixture; });
}

TEST(GoldenCoverage, EveryFixtureIsParsedOrListedAsNotParsed) {
	for (const std::filesystem::directory_entry& entry :
		 std::filesystem::directory_iterator(SPC_FIXTURES_DIR)) {
		const std::string name = entry.path().filename().string();
		// Dotfiles such as a Finder .DS_Store are not fixtures.
		if (name == "README.md" || name == "SHA256SUMS" || name.starts_with('.')) {
			continue;
		}
		EXPECT_TRUE(has_case(name) || std::ranges::find(kNotParsed, name) != kNotParsed.end())
			<< name << " needs a golden case in test_golden.cpp, or a kNotParsed entry";
	}
	for (const std::string_view name : kNotParsed) {
		EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(SPC_FIXTURES_DIR) / name))
			<< name << " is listed in kNotParsed but missing";
	}
}

TEST(GoldenCoverage, EveryGoldenFileHasACase) {
	for (const std::filesystem::directory_entry& entry :
		 std::filesystem::directory_iterator(SPC_GOLDEN_DIR)) {
		const std::string name = entry.path().filename().string();
		if (name.starts_with('.')) {
			continue; // .DS_Store and editor files
		}
		EXPECT_TRUE(name.ends_with(".txt") && has_case(name.substr(0, name.size() - 4)))
			<< name << " has no golden case; delete it";
	}
}

} // namespace

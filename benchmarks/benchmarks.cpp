/// @file benchmarks.cpp
/// @brief Parser and client benchmarks over the captured fixtures.
///
/// Each benchmark also reports the heap use of one call, measured outside
/// the timing loop: `allocs` and `alloc_bytes` (operator new calls and the
/// bytes they asked for), `peak_bytes` (the most memory live at once), and
/// `retained_bytes` (what the result holds). On macOS it reports
/// `instructions` per iteration too, which barely moves with machine load.

#include "models/json.hpp"
#include "spc/api.hpp"
#include "spc/http_client.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"
#include "support/allocation_counter.hpp"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

// Google Benchmark exits 0 even when a benchmark fails, so main checks this.
int failures = 0;

void fail(benchmark::State& state, const std::string& reason) {
	++failures;
	state.SkipWithError(reason);
}

/// The fixture's bytes, or "" if it cannot be read.
std::string read_fixture(std::string_view name) {
	const std::ifstream file(std::filesystem::path(SPC_FIXTURES_DIR) / name, std::ios::binary);
	if (!file) {
		return {};
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();
	return std::move(buffer).str();
}

// Timing a call that produced nothing would measure the wrong thing.
bool has_output(const spc::CategoricalOutlookPayload& p) {
	return !p.features.empty();
}
bool has_output(const spc::ProbOutlookPayload& p) {
	return !p.features.empty();
}
bool has_output(const spc::ConditionalIntensityPayload& p) {
	return !p.features.empty();
}
bool has_output(const spc::Day48OutlookPayload& p) {
	return !p.features.empty();
}
bool has_output(const spc::FireWeatherPayload& p) {
	return !p.features.empty();
}
bool has_output(const spc::MesoscalePayload& p) {
	return !p.discussions.empty();
}
bool has_output(const spc::WatchPayload& p) {
	return !p.watches.empty();
}
bool has_output(const spc::StormReportPayload& p) {
	return !p.reports.empty();
}
bool has_output(const std::vector<std::string>& pages) {
	return !pages.empty();
}
bool has_output(const glz::expected<spc::detail::Json, std::string>& root) {
	return root.has_value();
}
bool has_output(const spc::detail::ParsedNumber& number) {
	return number.ok;
}
template <typename T>
bool has_output(const spc::Result<T>& result) {
	return result.has_value() && has_output(*result);
}

/// Instructions this process has retired so far, or 0 where the OS doesn't
/// report them.
std::uint64_t instructions_retired() noexcept {
#if defined(__APPLE__)
	rusage_info_v4 info{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&info)) == 0) {
		return info.ri_instructions;
	}
#endif
	return 0;
}

/// Times `call`, which reads `bytes` of input, and reports the heap use of
/// one warm call.
template <typename Call>
void run(benchmark::State& state, std::size_t bytes, const Call& call) {
	using Output = std::invoke_result_t<const Call&>;
	if (!has_output(call())) {
		fail(state, "the call produced no output");
		return;
	}
	spc::test::AllocationStats heap;
	{
		const spc::test::AllocationProbe probe;
		Output output = call();
		benchmark::DoNotOptimize(output);
		// Read while `output` is alive, so its memory counts as retained.
		heap = probe.stats();
	}
	const std::uint64_t first = instructions_retired();
	for (auto _ : state) { // auto-ok: Google Benchmark loop idiom
		Output output = call();
		benchmark::DoNotOptimize(output);
	}
	const std::uint64_t last = instructions_retired();
	if (first != 0 && last > first) {
		state.counters["instructions"] = benchmark::Counter(static_cast<double>(last - first),
															benchmark::Counter::kAvgIterations);
	}
	state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
	state.counters["allocs"] = static_cast<double>(heap.allocations);
	state.counters["alloc_bytes"] = static_cast<double>(heap.bytes);
	state.counters["peak_bytes"] = static_cast<double>(heap.peak_bytes);
	state.counters["retained_bytes"] = static_cast<double>(heap.retained_bytes);
}

/// Serves one body for every request. It appends the body in libcurl's
/// 16 KiB write chunks, as HttpClient does, so the string grows the same way.
class FixtureTransport final : public spc::HttpTransport {
public:
	explicit FixtureTransport(std::string body) : body_(std::move(body)) {}

	[[nodiscard]] spc::Result<spc::HttpResponse> get(std::string_view /*path*/) const override {
		spc::HttpResponse response{200, {}, {}};
		for (std::size_t offset = 0; offset < body_.size(); offset += kCurlWriteChunk) {
			response.body.append(body_, offset, kCurlWriteChunk);
		}
		return response;
	}

private:
	static constexpr std::size_t kCurlWriteChunk = 16384; // CURL_MAX_WRITE_SIZE
	std::string body_;
};

/// Times `parse` over a fixture's bytes.
template <typename Parse>
void fixture_benchmark(benchmark::State& state, std::string_view fixture, const Parse& parse) {
	const std::string body = read_fixture(fixture);
	if (body.empty()) {
		fail(state, std::format("cannot read {}", fixture));
		return;
	}
	run(state, body.size(), [&body, &parse] { return parse(std::string_view{body}); });
}

/// Times `query` on a `Client` whose transport serves the fixture. `requests`
/// is how many responses one call reads.
template <typename Client, typename Query>
void client_benchmark(benchmark::State& state, std::string_view fixture, std::size_t requests,
					  const Query& query) {
	std::string body = read_fixture(fixture);
	if (body.empty()) {
		fail(state, std::format("cannot read {}", fixture));
		return;
	}
	const std::size_t bytes = body.size() * requests;
	const Client client{std::make_shared<FixtureTransport>(std::move(body))};
	run(state, bytes, [&client, &query] { return query(client); });
}

/// BM_Parse/<product>: a public parser over its largest fixture.
template <typename Parse>
void parse_benchmark(std::string_view product, std::string_view fixture, Parse parse) {
	benchmark::RegisterBenchmark(
		std::format("BM_Parse/{}", product),
		[fixture, parse](benchmark::State& state) { fixture_benchmark(state, fixture, parse); });
}

/// BM_ParseTree/<fixture>: the Glaze tree that every parser builds first.
void tree_benchmark(std::string_view fixture) {
	benchmark::RegisterBenchmark(
		std::format("BM_ParseTree/{}", fixture), [fixture](benchmark::State& state) {
			fixture_benchmark(state, fixture,
							  [](std::string_view body) { return spc::detail::parse_root(body); });
		});
}

/// BM_ParseDouble/<text>: the locale-independent number parser.
void double_benchmark(std::string_view text) {
	benchmark::RegisterBenchmark(
		std::format("BM_ParseDouble/{}", text), [text](benchmark::State& state) {
			run(state, text.size(), [text] { return spc::detail::parse_double(text); });
		});
}

/// BM_ArcGIS/<query>: a typed query through an in-memory transport, which
/// adds URL building, paging, and the envelope check to the parse.
template <typename Query>
void arcgis_benchmark(std::string_view name, std::string_view fixture, std::size_t requests,
					  Query query) {
	benchmark::RegisterBenchmark(
		std::format("BM_ArcGIS/{}", name), [fixture, requests, query](benchmark::State& state) {
			client_benchmark<spc::ArcGISClient>(state, fixture, requests, query);
		});
}

void register_benchmarks() {
	parse_benchmark("categorical", "arcgis_day1_categorical.geojson",
					[](std::string_view body) { return spc::parse_categorical(body, 1); });
	parse_benchmark("probabilistic", "arcgis_day1_prob_wind.geojson", [](std::string_view body) {
		return spc::parse_probabilistic(body, 1, "wind");
	});
	parse_benchmark(
		"conditional_intensity", "arcgis_day1_torn_conditional_intensity.esri.json",
		[](std::string_view body) { return spc::parse_conditional_intensity(body, 1, "tornado"); });
	parse_benchmark("day4_8", "day4prob.nolyr.geojson",
					[](std::string_view body) { return spc::parse_day4_8(body, 4); });
	parse_benchmark("fire_weather", "arcgis_day1_fire_weather.esri.json",
					[](std::string_view body) {
						return spc::parse_fire_weather(body, 1, spc::FireWeatherLayer::Outlook);
					});
	parse_benchmark("mesoscale", "arcgis_mesoscale_discussion.esri.json",
					[](std::string_view body) { return spc::parse_mesoscale_discussions(body); });
	parse_benchmark("watches", "iem_spc_watch.json",
					[](std::string_view body) { return spc::parse_watches(body); });
	parse_benchmark("storm_reports", "iem_storm_reports.json",
					[](std::string_view body) { return spc::parse_storm_reports(body); });

	// The parse fixtures, plus the Esri copy of the categorical outlook:
	// parse_categorical reads only GeoJSON.
	for (const std::string_view fixture : {
			 "arcgis_day1_categorical.geojson",
			 "arcgis_day1_categorical.esri.json",
			 "arcgis_day1_prob_wind.geojson",
			 "arcgis_day1_torn_conditional_intensity.esri.json",
			 "day4prob.nolyr.geojson",
			 "arcgis_day1_fire_weather.esri.json",
			 "arcgis_mesoscale_discussion.esri.json",
			 "iem_spc_watch.json",
			 "iem_storm_reports.json",
		 }) {
		tree_benchmark(fixture);
	}

	for (const std::string_view text : {"0.15", "5", "-97.3456"}) {
		double_benchmark(text);
	}

	arcgis_benchmark("categorical", "arcgis_day1_categorical.geojson", 1,
					 [](const spc::ArcGISClient& client) { return client.query_categorical(1); });
	arcgis_benchmark(
		"probabilistic", "arcgis_day1_prob_wind.geojson", 1,
		[](const spc::ArcGISClient& client) { return client.query_probabilistic(1, "wind"); });
	arcgis_benchmark("conditional_intensity", "arcgis_day1_torn_conditional_intensity.esri.json", 1,
					 [](const spc::ArcGISClient& client) {
						 return client.query_conditional_intensity(1, "tornado");
					 });
	arcgis_benchmark("day4_8", "arcgis_day4_8_nonempty.synthetic.json", 1,
					 [](const spc::ArcGISClient& client) { return client.query_day4_8(4); });
	// Day 1 fire weather merges two layers, so the transport answers twice.
	arcgis_benchmark("fire_weather", "arcgis_day1_fire_weather.esri.json", 2,
					 [](const spc::ArcGISClient& client) { return client.query_fire_weather(1); });
	arcgis_benchmark("active_md", "arcgis_mesoscale_discussion.esri.json", 1,
					 [](const spc::ArcGISClient& client) { return client.query_active_md(); });
	arcgis_benchmark("layer", "arcgis_day1_categorical.esri.json", 1,
					 [](const spc::ArcGISClient& client) {
						 return client.query_layer(spc::ArcGISService::Outlooks, 1, {});
					 });

	benchmark::RegisterBenchmark("BM_StaticFeed/categorical_day1", [](benchmark::State& state) {
		client_benchmark<spc::StaticFeedClient>(
			state, "day1otlk_cat.nolyr.geojson", 1,
			[](const spc::StaticFeedClient& client) { return client.day_categorical(1); });
	});
}

} // namespace

int main(int argc, char** argv) {
	benchmark::MaybeReenterWithoutASLR(argc, argv);
	register_benchmarks();
	benchmark::Initialize(&argc, argv);
	if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
		return 1;
	}
	benchmark::RunSpecifiedBenchmarks();
	benchmark::Shutdown();
	return failures == 0 ? 0 : 1;
}

// Pull every SDK library into a shared consumer, without making network calls.
#include "spc/spc.hpp"

#include <cstddef>
#include <string_view>

std::size_t spc_consumer_report_count(std::string_view body) {
	return spc::parse_storm_reports(body).reports.size();
}

spc::Result<spc::HttpResponse> spc_consumer_get(std::string_view url) {
	return spc::HttpClient{}.get(url);
}

spc::Result<spc::Day48OutlookPayload> spc_consumer_invalid_day() {
	return spc::ArcGISClient{}.query_day4_8(3);
}

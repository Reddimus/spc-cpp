// Compiles the whole public API without Glaze on the include path and checks
// a few calls that need no network.

#include "spc/spc.hpp"

#include <cstdlib>
#include <string_view>

int main() {
	const spc::ArcGISClient arcgis;
	const spc::StaticFeedClient feeds;
	const spc::Result<spc::Day48OutlookPayload> bad_day = arcgis.query_day4_8(3);
	const spc::Result<spc::CategoricalOutlookPayload> bad_category = feeds.day_categorical(4);
	const spc::CategoricalOutlookPayload parsed = spc::parse_categorical(
		R"({"features":[{"properties":{"LABEL":"SLGT"},"geometry":{"type":"Polygon",)"
		R"("coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]})",
		1);

	const bool passed = spc::version() == std::string_view{SPC_VERSION_STRING} && !bad_day &&
						bad_day.error().code == spc::ErrorCode::InvalidRequest && !bad_category &&
						bad_category.error().code == spc::ErrorCode::InvalidRequest &&
						parsed.features.size() == 1 &&
						spc::point_in_feature(0.5, 0.5, parsed.features.front());
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include "spc/spc.hpp"

#include <cstdlib>

int main() {
	spc::ArcGISPager pager{100};
	const spc::ClientConfig config;
	spc::ArcGISClient arcgis;
	spc::StaticFeedClient feeds;
	const spc::Result<spc::Day48OutlookPayload> bad_day = arcgis.query_day4_8(3);
	const spc::Result<spc::CategoricalOutlookPayload> bad_category = feeds.day_categorical(4);
	const bool passed = pager.page_size() == 100 && config.verify_ssl && !bad_day &&
						bad_day.error().code == spc::ErrorCode::InvalidRequest && !bad_category &&
						bad_category.error().code == spc::ErrorCode::InvalidRequest;
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

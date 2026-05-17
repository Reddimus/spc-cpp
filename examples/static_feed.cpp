/// @file static_feed.cpp
/// @brief Fetch the Day-1 categorical outlook via the static-feed fallback.

#include "spc/spc.hpp"

#include <iostream>

int main() {
	spc::StaticFeedClient client;
	spc::Result<spc::CategoricalOutlookPayload> r = client.day_categorical(1);
	if (!r) {
		if (r.error().is_feed_unavailable()) {
			std::cout << "Day-1 categorical: no active outlook (SPC 404 — normal)\n";
			return 0;
		}
		std::cerr << "error: " << r.error().message << "\n";
		return 1;
	}
	std::cout << "Day-1 categorical: " << r->features.size() << " feature(s)\n";
	for (const spc::OutlookFeature& f : r->features) {
		std::cout << "  " << f.label << " (severity " << static_cast<int>(f.severity)
				  << ") rings=" << f.rings.size() << " valid " << f.valid_from << " -> "
				  << f.valid_until << "\n";
	}
	return 0;
}

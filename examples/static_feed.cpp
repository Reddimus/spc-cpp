/// @file static_feed.cpp
/// @brief Day 1 categorical outlook from SPC's static GeoJSON feed.

#include "spc/spc.hpp"

#include <iostream>

int main() {
	const spc::StaticFeedClient client;
	const spc::Result<spc::CategoricalOutlookPayload> outlook = client.day_categorical(1);
	if (!outlook) {
		if (outlook.error().is_feed_unavailable()) {
			std::cout << "No day 1 outlook is issued right now.\n";
			return 0;
		}
		std::cerr << "error: " << outlook.error().message << "\n";
		return 1;
	}
	std::cout << "Day 1 categorical: " << outlook->features.size() << " band(s)\n";
	for (const spc::OutlookFeature& band : outlook->features) {
		std::cout << "  " << band.label << " (severity " << static_cast<int>(band.severity) << "), "
				  << band.rings.size() << " polygon(s), valid " << band.valid_from << " to "
				  << band.valid_until << "\n";
	}
	return 0;
}

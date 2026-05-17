/// @file parse_outlook.cpp
/// @brief Minimal example: parse an SPC categorical GeoJSON body and print
/// the per-severity feature summary. (PR-2 adds the network-fetching
/// static-feed / ArcGIS / archive examples.)

#include "spc/spc.hpp"

#include <iostream>
#include <string>

int main() {
	// A tiny inline day-1 categorical outlook (SLGT over a box).
	const std::string body = R"({
		"type": "FeatureCollection",
		"features": [{
			"type": "Feature",
			"properties": {"LABEL": "SLGT", "ISSUE": "202605170100",
						   "VALID": "202605171200", "EXPIRE": "202605181200"},
			"geometry": {"type": "Polygon",
						 "coordinates": [[[-98,34],[-94,34],[-94,38],[-98,38],[-98,34]]]}
		}]
	})";

	const spc::CategoricalOutlookPayload p = spc::parse_categorical(body, 1);
	std::cout << "day_offset=" << p.day_offset << " features=" << p.features.size() << "\n";
	for (const spc::OutlookFeature& f : p.features) {
		std::cout << "  label=" << f.label << " severity=" << static_cast<int>(f.severity)
				  << " rings=" << f.rings.size() << " valid_from=" << f.valid_from << "\n";
	}
	return 0;
}

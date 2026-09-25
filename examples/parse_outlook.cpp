/// @file parse_outlook.cpp
/// @brief Parse a categorical outlook body without any network access.

#include "spc/spc.hpp"

#include <iostream>
#include <string>

int main() {
	// A one-band day 1 outlook: slight risk over a box in Oklahoma and Kansas.
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

	const spc::CategoricalOutlookPayload outlook = spc::parse_categorical(body, 1);
	for (const spc::OutlookFeature& band : outlook.features) {
		std::cout << band.label << " (severity " << static_cast<int>(band.severity) << ") valid "
				  << band.valid_from << " to " << band.valid_until << "\n";
		std::cout << "  Wichita inside: " << std::boolalpha
				  << spc::point_in_feature(-97.34, 37.69, band) << "\n";
	}
	return 0;
}

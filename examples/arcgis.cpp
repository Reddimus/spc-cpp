/// @file arcgis.cpp
/// @brief Day 1 categorical and tornado outlooks, and active mesoscale
/// discussions, from NOAA's ArcGIS services.

#include "spc/spc.hpp"

#include <iostream>

int main() {
	const spc::ArcGISClient client;

	const spc::Result<spc::CategoricalOutlookPayload> categorical = client.query_categorical(1);
	if (!categorical) {
		std::cerr << "categorical: " << categorical.error().message << "\n";
		return 1;
	}
	std::cout << "Day 1 categorical: " << categorical->features.size() << " band(s)\n";
	for (const spc::OutlookFeature& band : categorical->features) {
		std::cout << "  " << band.label << " (severity " << static_cast<int>(band.severity)
				  << ")\n";
	}

	const spc::Result<spc::ProbOutlookPayload> tornado = client.query_probabilistic(1, "tornado");
	if (!tornado) {
		std::cerr << "tornado: " << tornado.error().message << "\n";
		return 1;
	}
	std::cout << "Day 1 tornado: " << tornado->features.size() << " isopleth(s)\n";
	for (const spc::ProbOutlookFeature& isopleth : tornado->features) {
		std::cout << "  " << isopleth.probability * 100.0 << "%\n";
	}

	const spc::Result<spc::MesoscalePayload> discussions = client.query_active_md();
	if (!discussions) {
		std::cerr << "mesoscale discussions: " << discussions.error().message << "\n";
		return 1;
	}
	std::cout << "Active mesoscale discussions: " << discussions->discussions.size() << "\n";
	for (const spc::MesoscaleDiscussion& md : discussions->discussions) {
		std::cout << "  " << md.name << " " << md.url << "\n";
	}
	return 0;
}

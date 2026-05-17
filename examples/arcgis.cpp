/// @file arcgis.cpp
/// @brief Query the Day-1 categorical + probabilistic-tornado outlooks via
/// the primary ArcGIS MapServer path (with ArcGISPager paging).

#include "spc/spc.hpp"

#include <iostream>

int main() {
	spc::ArcGISClient client;

	spc::Result<spc::CategoricalOutlookPayload> cat = client.query_categorical(1);
	if (!cat) {
		std::cerr << "categorical error: " << cat.error().message << "\n";
		return 1;
	}
	std::cout << "ArcGIS Day-1 categorical: " << cat->features.size() << " feature(s)\n";
	for (const spc::OutlookFeature& f : cat->features) {
		std::cout << "  " << f.label << " sev=" << static_cast<int>(f.severity)
				  << " rings=" << f.rings.size() << "\n";
	}

	spc::Result<spc::ProbOutlookPayload> torn = client.query_probabilistic(1, "tornado");
	if (!torn) {
		std::cerr << "probabilistic error: " << torn.error().message << "\n";
		return 1;
	}
	std::cout << "ArcGIS Day-1 prob tornado: " << torn->features.size() << " isopleth(s)\n";
	for (const spc::ProbOutlookFeature& f : torn->features) {
		std::cout << "  p=" << f.probability << " rings=" << f.rings.size() << "\n";
	}
	return 0;
}

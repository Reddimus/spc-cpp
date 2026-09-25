/// @file fire_weather.cpp
/// @brief Fire-weather outlooks for days 1 to 3.

#include "spc/spc.hpp"

#include <cstdint>
#include <iostream>

namespace {

const char* layer_name(spc::FireWeatherLayer layer) {
	switch (layer) {
		case spc::FireWeatherLayer::Outlook:
			return "outlook";
		case spc::FireWeatherLayer::DryThunderstorm:
			return "dry thunderstorm";
		case spc::FireWeatherLayer::WindLowHumidity:
			return "wind and low humidity";
	}
	return "unknown";
}

} // namespace

int main() {
	const spc::ArcGISClient client;
	for (std::int32_t day = 1; day <= 3; ++day) {
		const spc::Result<spc::FireWeatherPayload> outlook = client.query_fire_weather(day);
		if (!outlook) {
			std::cerr << "day " << day << ": " << outlook.error().message << "\n";
			return 1;
		}
		std::cout << "Day " << day << ": " << outlook->features.size() << " area(s)\n";
		for (const spc::FireWeatherFeature& area : outlook->features) {
			std::cout << "  " << layer_name(area.layer) << ": ";
			if (day <= 2) {
				std::cout << (area.label.empty() ? "unlabeled" : area.label) << "\n";
			} else {
				std::cout << area.probability * 100.0 << "%\n";
			}
		}
	}
	return 0;
}

/// @file archive.cpp
/// @brief Storm reports from one NWS office over one day, via IEM.

#include "spc/spc.hpp"

#include <cstddef>
#include <iostream>

int main() {
	const spc::ArchiveClient client;
	const spc::Result<spc::StormReportPayload> reports =
		client.storm_reports("2024-04-26T12:00Z", "2024-04-27T12:00Z", "ICT");
	if (!reports) {
		std::cerr << "error: " << reports.error().message << "\n";
		return 1;
	}
	std::cout << "Storm reports from NWS Wichita, 2024-04-26 to 27: " << reports->reports.size()
			  << "\n";
	std::size_t shown = 0;
	for (const spc::StormReport& report : reports->reports) {
		if (shown++ == 8) {
			break;
		}
		std::cout << "  " << report.valid_at << " " << report.type_text;
		if (report.magnitude > 0.0) {
			std::cout << " " << report.magnitude << " " << report.unit;
		}
		std::cout << ", " << report.city << " " << report.state << "\n";
	}
	return 0;
}

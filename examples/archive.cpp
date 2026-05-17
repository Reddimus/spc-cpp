/// @file archive.cpp
/// @brief Pull historical Local Storm Reports for a window via the IEM
/// archive client (best-effort; conservative rate/retry).

#include "spc/spc.hpp"

#include <iostream>

int main() {
	spc::ArchiveClient client;
	spc::Result<spc::StormReportPayload> r =
		client.storm_reports("2024-04-26T12:00Z", "2024-04-27T12:00Z", "ICT");
	if (!r) {
		std::cerr << "archive error: " << r.error().message << "\n";
		return 1;
	}
	std::cout << "LSRs 2024-04-26..27 (WFO ICT): " << r->reports.size() << "\n";
	std::size_t shown = 0;
	for (const spc::StormReport& sr : r->reports) {
		if (shown++ >= 8) {
			break;
		}
		std::cout << "  " << sr.valid_at << " " << sr.type_text << " mag=" << sr.magnitude << " "
				  << sr.unit << " @ " << sr.city << ", " << sr.county << " " << sr.state << "\n";
	}
	return 0;
}

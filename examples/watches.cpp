/// @file watches.cpp
/// @brief SPC watches in effect now, and on a past date, via IEM.

#include "spc/spc.hpp"

#include <iostream>
#include <string_view>

namespace {

int print_watches(const spc::ArchiveClient& client, std::string_view timestamp) {
	const spc::Result<spc::WatchPayload> watches = client.watches(timestamp);
	if (!watches) {
		std::cerr << "error: " << watches.error().message << "\n";
		return 1;
	}
	std::cout << (timestamp.empty() ? std::string_view{"now"} : timestamp) << ": "
			  << watches->watches.size() << " watch(es)\n";
	for (const spc::Watch& watch : watches->watches) {
		std::cout << "  " << watch.type << " #" << watch.number << "/" << watch.year
				  << (watch.is_pds ? " (PDS)" : "") << ", hail to " << watch.max_hail_size
				  << " in, until " << watch.expires_at << "\n";
	}
	return 0;
}

} // namespace

int main() {
	const spc::ArchiveClient client;
	if (print_watches(client, {}) != 0) {
		return 1;
	}
	return print_watches(client, "202404262200");
}

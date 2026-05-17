#include "spc/error.hpp"

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>
#include <string>

namespace spc {

Error Error::from_response(int status, const std::string& body) {
	Error err;
	err.http_status = status;

	// Determine error code from HTTP status. SPC serves a static HTML 404
	// page when a product has no active outlook (the documented normal
	// overnight state for e.g. day-1 probabilistic); we surface that as
	// FeedUnavailable so callers clear rows rather than treat it as a fault.
	if (status == 404) {
		err.code = ErrorCode::FeedUnavailable;
	} else if (status == 400) {
		err.code = ErrorCode::InvalidRequest;
	} else if (status == 429 || status == 503) {
		err.code = ErrorCode::RateLimited;
	} else if (status >= 500) {
		err.code = ErrorCode::ServerError;
	} else {
		err.code = ErrorCode::Unknown;
	}

	// Best-effort: ArcGIS returns JSON `{"error":{"code":...,"message":...}}`
	// on logical failures even with HTTP 200. Parse via glz::generic (no
	// schema to reject on); fall back to the raw body for SPC's HTML pages.
	glz::generic root{};
	glz::error_ctx ec = glz::read_json(root, body);
	if (!ec && root.is_object()) {
		const glz::generic::object_t& obj = root.get_object();
		glz::generic::object_t::const_iterator it = obj.find("error");
		if (it != obj.end() && it->second.is_object()) {
			const glz::generic::object_t& eo = it->second.get_object();
			glz::generic::object_t::const_iterator m = eo.find("message");
			if (m != eo.end() && m->second.is_string()) {
				err.message = m->second.get<std::string>();
			}
			glz::generic::object_t::const_iterator d = eo.find("details");
			if (d != eo.end() && d->second.is_string()) {
				err.detail = d->second.get<std::string>();
			}
		}
	} else {
		// Not valid JSON (SPC's HTML 404) — keep a short raw snippet.
		err.message = body.substr(0, 256);
	}

	if (err.message.empty()) {
		err.message = "HTTP " + std::to_string(status);
	}

	return err;
}

} // namespace spc

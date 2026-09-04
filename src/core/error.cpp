#include "spc/error.hpp"

#include <format>
#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>
#include <string>

namespace spc {

namespace {

/// Best-effort: ArcGIS returns JSON `{"error":{"code":...,"message":...}}`
/// on logical failures even with HTTP 200. Parse via glz::generic (no schema
/// to reject on); fall back to the raw body for SPC's HTML pages.
void fill_message_from_body(Error& err, const std::string& body) {
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
}

} // namespace

Error Error::from_response(int status, const std::string& body, Feed404 semantics) {
	Error err;
	err.http_status = status;

	// Determine error code from HTTP status. SPC serves a static HTML 404
	// page when a product has no active outlook (the documented normal
	// overnight state for e.g. day-1 probabilistic); only a caller that
	// addressed those feeds may ask for that reading. Every other 404 — a
	// renamed MapServer path, a retired IEM endpoint, a typo in a base URL —
	// is a genuine fault, so the default is NotFound.
	if (status == 404) {
		err.code = semantics == Feed404::NoActiveOutlook ? ErrorCode::FeedUnavailable
														 : ErrorCode::NotFound;
	} else if (status == 400) {
		err.code = ErrorCode::InvalidRequest;
	} else if (status == 429 || status == 503) {
		err.code = ErrorCode::RateLimited;
	} else if (status >= 500) {
		err.code = ErrorCode::ServerError;
	} else {
		err.code = ErrorCode::Unknown;
	}

	fill_message_from_body(err, body);

	if (err.message.empty()) {
		err.message = "HTTP " + std::to_string(status);
	}

	return err;
}

Error Error::from_arcgis(int arcgis_code, const std::string& body) {
	Error err;

	// ArcGIS codes overlap the HTTP status space but are not HTTP statuses:
	// the transport answered 200. Keep the code in http_status only while it
	// is HTTP-shaped, so a code such as 1000 cannot masquerade as a status.
	err.http_status = arcgis_code >= 100 && arcgis_code <= 599 ? arcgis_code : 0;

	if (arcgis_code == 404) {
		// A renamed or retired MapServer path. Never "no active outlook":
		// ArcGIS signals an empty product as HTTP 200 with no features.
		err.code = ErrorCode::NotFound;
	} else if (arcgis_code == 429) {
		err.code = ErrorCode::RateLimited;
	} else if (arcgis_code >= 500 && arcgis_code <= 599) {
		err.code = ErrorCode::ServerError;
	} else {
		// 400, 403, 498/499 (token), 1000+ (operation failures): the request
		// as posed cannot be served.
		err.code = ErrorCode::InvalidRequest;
	}

	fill_message_from_body(err, body);

	if (err.detail.empty()) {
		err.detail = std::format("arcgisCode={}", arcgis_code);
	}
	if (err.message.empty()) {
		err.message = std::format("ArcGIS error {}", arcgis_code);
	}

	return err;
}

} // namespace spc

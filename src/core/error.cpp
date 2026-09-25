#include "spc/error.hpp"

#include <format>
#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>
#include <string>

namespace spc {

namespace {

/// Use the ArcGIS error message when the body is JSON; otherwise keep the start
/// of the raw body, such as SPC's HTML 404 page.
void fill_message_from_body(Error& err, const std::string& body) {
	glz::generic root{};
	const glz::error_ctx ec = glz::read_json(root, body);
	if (!ec && root.is_object()) {
		const glz::generic::object_t& obj = root.get_object();
		glz::generic::object_t::const_iterator it = obj.find("error");
		if (it != obj.end() && it->second.is_object()) {
			const glz::generic::object_t& eo = it->second.get_object();
			glz::generic::object_t::const_iterator m = eo.find("message");
			if (m != eo.end() && m->second.is_string()) {
				err.message = m->second.get<std::string>();
			}
			// ArcGIS sends `details` as an array of strings.
			glz::generic::object_t::const_iterator d = eo.find("details");
			if (d != eo.end() && d->second.is_string()) {
				err.detail = d->second.get<std::string>();
			} else if (d != eo.end() && d->second.is_array()) {
				for (const glz::generic& item : d->second.get_array()) {
					if (item.is_string() && !item.get<std::string>().empty()) {
						err.detail += (err.detail.empty() ? "" : "; ") + item.get<std::string>();
					}
				}
			}
		}
	} else {
		err.message = body.substr(0, 256);
	}
}

} // namespace

Error Error::from_response(int status, const std::string& body, Feed404 semantics) {
	Error err;
	err.http_status = status;

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

	// ArcGIS also uses codes such as 1000 that are not HTTP statuses.
	err.http_status = arcgis_code >= 100 && arcgis_code <= 599 ? arcgis_code : 0;

	if (arcgis_code == 404) {
		// A retired service path. An empty product is HTTP 200 with no features.
		err.code = ErrorCode::NotFound;
	} else if (arcgis_code == 429 || arcgis_code == 503) {
		// The same codes as from_response.
		err.code = ErrorCode::RateLimited;
	} else if (arcgis_code >= 500 && arcgis_code <= 599) {
		err.code = ErrorCode::ServerError;
	} else {
		// 400, 403, 498/499 (token), and 1000+ (operation failures).
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

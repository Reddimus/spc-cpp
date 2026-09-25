#include "spc/version.hpp"

namespace spc {

std::string_view version() noexcept {
	return SPC_VERSION_STRING;
}

} // namespace spc

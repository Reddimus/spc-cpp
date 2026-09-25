/// @file version.hpp
/// @brief spc-cpp version. The single source of truth: CMakeLists.txt reads
/// the three numbers below.

#pragma once

#include <string_view>

// Macros, so they also work in #if.
// NOLINTBEGIN(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)
#define SPC_VERSION_MAJOR 0
#define SPC_VERSION_MINOR 4
#define SPC_VERSION_PATCH 0

#define SPC_VERSION_STRINGIFY_(x) #x
#define SPC_VERSION_STRINGIFY(x) SPC_VERSION_STRINGIFY_(x)
/// e.g. "0.4.0"
#define SPC_VERSION_STRING                   \
	SPC_VERSION_STRINGIFY(SPC_VERSION_MAJOR) \
	"." SPC_VERSION_STRINGIFY(SPC_VERSION_MINOR) "." SPC_VERSION_STRINGIFY(SPC_VERSION_PATCH)
// NOLINTEND(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)

namespace spc {

/// Version of the linked library. Compare with SPC_VERSION_STRING to catch a
/// header/library mismatch.
[[nodiscard]] std::string_view version() noexcept;

} // namespace spc

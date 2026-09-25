/// @file fixtures.hpp
/// @brief Read a captured payload from tests/fixtures.

#pragma once

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace spc::test {

inline std::string read_fixture(const std::string& name) {
	std::ifstream file(std::filesystem::path(SPC_FIXTURES_DIR) / name, std::ios::binary);
	EXPECT_TRUE(file.is_open()) << "missing fixture: " << name;
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

} // namespace spc::test

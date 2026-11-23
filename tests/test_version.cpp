#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// The version is set once, in the root CMakeLists.txt's project() line; scripts/version.sh reads that line and
// the DLL gets it as FR_VERSION.

namespace {

std::string ReadRepoFile(const char* p_Relative) {
    const std::filesystem::path path = std::filesystem::path(FR_REPO_ROOT) / p_Relative;
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot read " << path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("version: the project line is the one scripts/version.sh reads, and FR_VERSION is its version") {
    const std::string cmake = ReadRepoFile("CMakeLists.txt");
    // One line, at the start of a line: project(FreelancerRandomizer VERSION <x.y.z> LANGUAGES C CXX)
    const std::string prefix = "project(FreelancerRandomizer VERSION ";
    const std::string suffix = " LANGUAGES C CXX)";
    size_t at = cmake.rfind(prefix, 0) == 0 ? 0 : cmake.find("\n" + prefix);
    REQUIRE(at != std::string::npos);
    at += cmake[at] == '\n' ? prefix.size() + 1 : prefix.size();

    // x.y.z: three dot-separated runs of digits.
    const size_t begin = at;
    for (int part = 0; part < 3; ++part) {
        const size_t digits = at;
        while (at < cmake.size() && cmake[at] >= '0' && cmake[at] <= '9')
            ++at;
        REQUIRE(at > digits);
        if (part < 2) {
            REQUIRE(at < cmake.size());
            REQUIRE(cmake[at] == '.');
            ++at;
        }
    }
    const std::string version = cmake.substr(begin, at - begin);

    REQUIRE(cmake.compare(at, suffix.size(), suffix) == 0);
    at += suffix.size();
    if (at < cmake.size() && cmake[at] == '\r')
        ++at;
    REQUIRE(at < cmake.size());
    CHECK(cmake[at] == '\n');
    CHECK(version == FR_VERSION);
}

TEST_CASE("version: the DLL takes its version from CMake") {
    CHECK(ReadRepoFile("CMakeLists.txt")
              .find("target_compile_definitions(FreelancerRandomizer PRIVATE "
                    "FR_VERSION=\"${PROJECT_VERSION}\")") != std::string::npos);
    CHECK(ReadRepoFile("src/mod/FreelancerRandomizer.cpp").find("kVersion = FR_VERSION;") != std::string::npos);
}

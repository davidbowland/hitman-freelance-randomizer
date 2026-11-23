cmake_minimum_required(VERSION 3.24)
project(FreelancerRandomizerTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${REPO_ROOT}/src/core/*.cpp")
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
# The pure part of src/common (the package format).
set(COMMON_SOURCES
    "${REPO_ROOT}/src/common/PatchPackage.cpp"
    "${REPO_ROOT}/src/common/RecipeBuild.cpp"
    "${REPO_ROOT}/src/common/RpkgRead.cpp"
)

# Golden roll values must not depend on FMA contraction.
set_source_files_properties(${CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# The mod's version, from the root project() line as scripts/version.sh reads it
# (tests/test_version.cpp checks the two agree).
file(STRINGS "${REPO_ROOT}/CMakeLists.txt" FR_PROJECT_LINE
     REGEX "^project\\(FreelancerRandomizer VERSION [0-9]+\\.[0-9]+\\.[0-9]+ ")
string(REGEX REPLACE "^project\\(FreelancerRandomizer VERSION ([0-9]+\\.[0-9]+\\.[0-9]+) .*" "\\1"
       FR_VERSION "${FR_PROJECT_LINE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO_ROOT}/CMakeLists.txt")

add_executable(fr_tests ${CORE_SOURCES} ${COMMON_SOURCES} ${TEST_SOURCES})
# The source-scanning tests (log gates, the keys the mod loads, the version) read files from the repository.
target_compile_definitions(fr_tests PRIVATE FR_REPO_ROOT="${REPO_ROOT}" FR_VERSION="${FR_VERSION}")
target_include_directories(fr_tests PRIVATE
    "${REPO_ROOT}/src/core"
    "${REPO_ROOT}/src/common"
)
target_include_directories(fr_tests SYSTEM PRIVATE "${REPO_ROOT}/third_party/devpkg/include")
target_compile_options(fr_tests PRIVATE -Wall -Wextra -Werror)

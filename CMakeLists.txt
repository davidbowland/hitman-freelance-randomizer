cmake_minimum_required(VERSION 3.24)

# Builds FreelancerRandomizer.dll against the ZHMModSDK v4.2.1
# DevPkg. Cross-built with cmake/clang-cl.cmake inside the fl-randomizer-build
# image (see scripts/build.sh). Host unit tests live in tests/ as a separate project.
# The version is set here only (scripts/version.sh reads this line); the DLL gets it
# as FR_VERSION.
project(FreelancerRandomizer VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/devpkg/CMakeLists.txt")
    message(FATAL_ERROR "SDK DevPkg missing -- run scripts/fetch-devpkg.sh")
endif ()

# Defines the imported ZHMModSDK target (and links bare fmt/imgui from release/lib).
add_subdirectory(third_party/devpkg devpkg)

# The SDK headers include <directx/d3d12.h> (Hooks.h, IModSDK.h), which comes from
# microsoft/DirectX-Headers (a vcpkg dependency upstream) and is not in the DevPkg.
# Header-only use: SOURCE_SUBDIR points at a directory without a CMakeLists.txt,
# so its own targets are not added.
include(FetchContent)
FetchContent_Declare(directx_headers
    URL https://github.com/microsoft/DirectX-Headers/archive/refs/tags/v1.721.2.tar.gz
    URL_HASH SHA256=b736c311057f2b426eb465d4e7867da4eb4e76d69be27f45bfa359189f97c0b0
    SOURCE_SUBDIR headers-only
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(directx_headers)

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Host-testable logic (src/core) and SDK glue (src/mod) go into FreelancerRandomizer,
# with the shared log and guards in src/common.
file(GLOB FR_CORE_SOURCES CONFIGURE_DEPENDS "${SRC}/core/*.cpp")
file(GLOB FR_MOD_SOURCES CONFIGURE_DEPENDS "${SRC}/mod/*.cpp")
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "${SRC}/common/*.cpp")

# No FMA contraction: keeps rolls stable across compiler versions; host test golden values
# may differ from the DLL in the last bit (different math libraries).
set_source_files_properties(${FR_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "/clang:-ffp-contract=off")

# One mod DLL: C++23, the DirectX headers, ZHMModSDK and implot. implot is needed
# because IPluginInterface::SetupUI (inline) calls ImPlot and the DevPkg's imported
# target does not link it.
function(add_mod_dll p_Name)
    cmake_parse_arguments(MOD "" "" "SOURCES;INCLUDES" ${ARGN})
    add_library(${p_Name} SHARED ${MOD_SOURCES})
    target_include_directories(${p_Name} PRIVATE ${MOD_INCLUDES})
    target_include_directories(${p_Name} SYSTEM PRIVATE "${directx_headers_SOURCE_DIR}/include")
    target_link_libraries(${p_Name} PRIVATE ZHMModSDK implot)
    set_target_properties(${p_Name} PROPERTIES OUTPUT_NAME ${p_Name} PREFIX "")
endfunction()

add_mod_dll(FreelancerRandomizer
    SOURCES ${FR_CORE_SOURCES} ${FR_MOD_SOURCES} ${COMMON_SOURCES}
    INCLUDES "${SRC}/core" "${SRC}/mod" "${SRC}/common"
)
target_compile_definitions(FreelancerRandomizer PRIVATE FR_VERSION="${PROJECT_VERSION}")
# /W4 /WX apply to our sources only: ZHMModSDK is an imported target and the DirectX
# headers are SYSTEM, so their headers don't warn. /EHa is on the whole target, so no file
# with __try guards can miss it. clang-cl does not fully model asynchronous exceptions,
# even with /EHa: src/common/Guard.h says what the guards catch.
target_compile_options(FreelancerRandomizer PRIVATE /W4 /WX /EHa)

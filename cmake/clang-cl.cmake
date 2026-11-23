set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Copy of ZHMModSDK v4.2.1 cmake/toolchains/clang-cl.cmake, without the in-tree
# .xwin fallback: XWIN_SPLAT_DIR is required (the fl-randomizer-build image sets it).
set(xwin "$ENV{XWIN_SPLAT_DIR}")
if (NOT xwin)
  message(FATAL_ERROR "XWIN_SPLAT_DIR is not set -- build inside the fl-randomizer-build image (scripts/build.sh)")
endif ()
if (NOT EXISTS "${xwin}/crt/include")
  message(FATAL_ERROR "no Windows SDK at ${xwin} -- run scripts/build.sh, which rebuilds the image")
endif ()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CodeView in the objects (/Z7), which lld-link's /DEBUG writes to FreelancerRandomizer.pdb. With
# FR_DWARF on (scripts/build.sh's default), also DWARF in the DLL, so GDB can read symbols
# straight out of it when debugging the game under Proton. CI turns it off for releases.
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-O2")
option(FR_DWARF "Also embed DWARF debug info in the DLL (for GDB under Proton)" OFF)
# Compile options rather than *_FLAGS_*_INIT, which only seed the cache on the first
# configure. This file can be read more than once, hence the IN_LIST check.
if (FR_DWARF)
  get_directory_property(_fr_opts COMPILE_OPTIONS)
  foreach (f /clang:-gdwarf-4 /clang:-fstandalone-debug)
    set(_fr_opt "$<$<CONFIG:Debug,RelWithDebInfo>:${f}>")
    if (NOT _fr_opt IN_LIST _fr_opts)
      add_compile_options("${_fr_opt}")
    endif ()
  endforeach ()
endif ()

foreach (d crt/include sdk/include/ucrt sdk/include/um sdk/include/shared sdk/include/winrt)
  string(APPEND inc " /imsvc${xwin}/${d}")
endforeach ()

set(CMAKE_C_FLAGS_INIT "${inc} -Wno-unused-command-line-argument")
set(CMAKE_CXX_FLAGS_INIT "${inc} -Wno-unused-command-line-argument /EHsc")

foreach (d crt/lib/x86_64 sdk/lib/um/x86_64 sdk/lib/ucrt/x86_64)
  string(APPEND lib " /libpath:${xwin}/${d}")
endforeach ()

set(CMAKE_EXE_LINKER_FLAGS_INIT "${lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${lib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${lib}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

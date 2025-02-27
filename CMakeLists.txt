cmake_minimum_required(VERSION 3.14)
project(FEX)

INCLUDE (CheckIncludeFiles)
CHECK_INCLUDE_FILES ("gdb/jit-reader.h" HAVE_GDB_JIT_READER_H)

option(BUILD_TESTS "Build unit tests to ensure sanity" TRUE)
option(BUILD_FEX_LINUX_TESTS "Build FEXLinuxTests, requires x86 compiler" FALSE)
option(BUILD_THUNKS "Build thunks" FALSE)
option(ENABLE_CLANG_THUNKS "Build thunks with clang" FALSE)
option(ENABLE_CLANG_FORMAT "Run clang format over the source" FALSE)
option(ENABLE_IWYU "Enables include what you use program" FALSE)
option(ENABLE_LTO "Enable LTO with compilation" TRUE)
option(ENABLE_XRAY "Enable building with LLVM X-Ray" FALSE)
option(ENABLE_LLD "Enable linking with lld" FALSE)
option(ENABLE_MOLD "Enable linking with mold" FALSE)
option(ENABLE_ASAN "Enables Clang ASAN" FALSE)
option(ENABLE_TSAN "Enables Clang TSAN" FALSE)
option(ENABLE_ASSERTIONS "Enables assertions in build" FALSE)
option(ENABLE_GDB_SYMBOLS "Enables GDBSymbols integration support" ${HAVE_GDB_JIT_READER_H})
option(ENABLE_VISUAL_DEBUGGER "Enables the visual debugger for compiling" FALSE)
option(ENABLE_STRICT_WERROR "Enables stricter -Werror for CI" FALSE)
option(ENABLE_WERROR "Enables -Werror" FALSE)
option(ENABLE_JEMALLOC "Enables jemalloc allocator" TRUE)
option(ENABLE_OFFLINE_TELEMETRY "Enables FEX offline telemetry" TRUE)
option(ENABLE_COMPILE_TIME_TRACE "Enables time trace compile option" FALSE)
option(ENABLE_LIBCXX "Enables LLVM libc++" FALSE)
option(ENABLE_INTERPRETER "Enables FEX's Interpreter" FALSE)
option(ENABLE_CCACHE "Enables ccache for compile caching" TRUE)
option(ENABLE_TERMUX_BUILD "Forces building for Termux on a non-Termux build machine" FALSE)
option(ENABLE_VIXL_SIMULATOR "Forces the FEX JIT to use the VIXL simulator" FALSE)
option(ENABLE_VIXL_DISASSEMBLER "Enables debug disassembler output with VIXL" FALSE)
option(COMPILE_VIXL_DISASSEMBLER "Compiles the vixl disassembler in to vixl" FALSE)
option(ENABLE_FEXCORE_PROFILER "Enables use of the FEXCore timeline profiling capabilities" FALSE)
set (FEXCORE_PROFILER_BACKEND "gpuvis" CACHE STRING "Set which backend you want to use for the FEXCore profiler")

set (X86_32_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain_x86_32.cmake" CACHE FILEPATH "Toolchain file for the (cross-)compiler targeting i686")
set (X86_64_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain_x86_64.cmake" CACHE FILEPATH "Toolchain file for the (cross-)compiler targeting x86_64")
set (DATA_DIRECTORY "${CMAKE_INSTALL_PREFIX}/share/fex-emu" CACHE PATH "global data directory")

if (ENABLE_FEXCORE_PROFILER)
  add_definitions(-DENABLE_FEXCORE_PROFILER=1)
  string(TOUPPER "${FEXCORE_PROFILER_BACKEND}" FEXCORE_PROFILER_BACKEND)

  if (FEXCORE_PROFILER_BACKEND STREQUAL "GPUVIS")
    add_definitions(-DFEXCORE_PROFILER_BACKEND=1)
  else()
    message(FATAL_ERROR "Unknown FEXCore profiler backend ${FEXCORE_PROFILER_BACKEND}")
  endif()
endif()

# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/cmake_uninstall.cmake)
endif()

# These options are meant for package management
set (TUNE_CPU "native" CACHE STRING "Override the CPU the build is tuned for")
set (TUNE_ARCH "generic" CACHE STRING "Override the Arch the build is tuned for")
set (OVERRIDE_VERSION "detect" CACHE STRING "Override the FEX version in the format of <MMYY>{.<REV>}")

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE)
if (CMAKE_BUILD_TYPE MATCHES "DEBUG")
  set(ENABLE_ASSERTIONS TRUE)
endif()

if (ENABLE_ASSERTIONS)
  message(STATUS "Assertions enabled")
  add_definitions(-DASSERTIONS_ENABLED=1)
endif()

if (ENABLE_GDB_SYMBOLS)
  message(STATUS "GDBSymbols support enabled")
  add_definitions(-DGDB_SYMBOLS_ENABLED=1)
endif()


if (ENABLE_INTERPRETER)
  message(STATUS "Interpreter enabled")
  add_definitions(-DINTERPRETER_ENABLED=1)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Bin)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
cmake_policy(SET CMP0083 NEW) # Follow new PIE policy
include(CheckPIESupported)
check_pie_supported()

if (ENABLE_LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  option(ENABLE_X86_HOST_DEBUG "Enables compiling on x86_64 host" FALSE)
  if (NOT ENABLE_X86_HOST_DEBUG)
    message(FATAL_ERROR
      " Be warned: FEX isn't optimized for x86_64 hosts!\n"
      " Support for x86_64 hosts is only for debugging and convenience!\n"
      " Don't expect amazing performance or optimal code generation!\n"
      " Pass -DENABLE_X86_HOST_DEBUG=True to bypass this message!")
  endif()
  set(_M_X86_64 1)
  add_definitions(-D_M_X86_64=1)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcx16")
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^aarch64|^arm64|^armv8\.*")
  set(_M_ARM_64 1)
  add_definitions(-D_M_ARM_64=1)
endif()

if (ENABLE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    message(STATUS "CCache enabled")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
  endif()
endif()

if (ENABLE_XRAY)
  add_compile_options(-fxray-instrument)
  link_libraries(-fxray-instrument)
endif()

if (ENABLE_COMPILE_TIME_TRACE)
  add_compile_options(-ftime-trace)
  link_libraries(-ftime-trace)
endif()

set (PTHREAD_LIB pthread)

if (ENABLE_LLD AND ENABLE_MOLD)
  message (FATAL_ERROR "Cannot enable both lld and mold")
elseif (ENABLE_LLD)
  set (LD_OVERRIDE "-fuse-ld=lld")
  add_link_options(${LD_OVERRIDE})
elseif (ENABLE_MOLD)
  add_link_options("-fuse-ld=mold")
endif()

if (ENABLE_LIBCXX)
  message(WARNING "This is an unsupported configuration and should only be used for testing")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -stdlib=libc++")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
endif()

if (NOT ENABLE_OFFLINE_TELEMETRY)
  # Disable FEX offline telemetry entirely if asked
  add_definitions(-DFEX_DISABLE_TELEMETRY=1)
endif()

if(DEFINED ENV{TERMUX_VERSION} OR ENABLE_TERMUX_BUILD)
  add_definitions(-DTERMUX_BUILD=1)
  set(TERMUX_BUILD 1)
  # Termux doesn't support Jemalloc due to bad interactions between emutls, jemalloc, and scudo
  set(ENABLE_JEMALLOC FALSE)
endif()

if (ENABLE_ASAN)
  add_definitions(-DENABLE_ASAN=1)
  add_compile_options(-fno-omit-frame-pointer -fsanitize=address -fsanitize-address-use-after-scope)
  link_libraries(-fno-omit-frame-pointer -fsanitize=address -fsanitize-address-use-after-scope)
endif()

if (ENABLE_TSAN)
  add_compile_options(-fno-omit-frame-pointer -fsanitize=thread)
  link_libraries(-fno-omit-frame-pointer -fsanitize=thread)
endif()

if (ENABLE_JEMALLOC)
  add_definitions(-DENABLE_JEMALLOC=1)
  add_subdirectory(External/jemalloc/)
  include_directories(External/jemalloc/pregen/include/)
else()
  message (STATUS
    " jemalloc disabled!\n"
    " This is not a recommended configuration!\n"
    " This will very explicitly break 32-bit application execution!\n"
    " Use at your own risk!")
endif()

set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")
set (CMAKE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_LINKER_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")

set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer")
set (CMAKE_LINKER_FLAGS_RELEASE "${CMAKE_LINKER_FLAGS_RELEASE} -fomit-frame-pointer")

include_directories(External/robin-map/include/)

if (BUILD_TESTS)
  # Enable vixl disassembler if tests are enabled.
  set(COMPILE_VIXL_DISASSEMBLER TRUE)
endif()

add_subdirectory(External/vixl/)
include_directories(External/vixl/src/)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # This means we were attempted to get compiled with GCC
  message(FATAL_ERROR "FEX doesn't support getting compiled with GCC!")
endif()

find_package(PkgConfig REQUIRED)
find_package(Python 3.0 REQUIRED COMPONENTS Interpreter)

add_subdirectory(External/xxhash/)
include_directories(External/xxhash/)

add_definitions(-Wno-trigraphs)
add_definitions(-DGLOBAL_DATA_DIRECTORY="${DATA_DIRECTORY}/")

if (BUILD_TESTS)
  option(CATCH_BUILD_STATIC_LIBRARY "" ON)
  set(CATCH_BUILD_STATIC_LIBRARY ON)
  add_subdirectory(External/Catch2/)

  # Pull in catch_discover_tests definition
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/Catch2/contrib/")
  include(Catch)
endif()

add_subdirectory(External/cpp-optparse/)
include_directories(External/cpp-optparse/)

add_subdirectory(External/fmt/)

add_subdirectory(External/imgui/)
include_directories(External/imgui/)

add_subdirectory(External/json-maker/)
include_directories(External/json-maker/)

add_subdirectory(External/tiny-json/)
include_directories(External/tiny-json/)

include_directories(External/xbyak/)

include_directories(Source/)
include_directories("${CMAKE_BINARY_DIR}/Source/")

include(CheckCXXCompilerFlag)

# Add in diagnostic colours if the option is available.
# Ninja code generator will kill colours if this isn't here
check_cxx_compiler_flag(-fdiagnostics-color=always GCC_COLOR)
check_cxx_compiler_flag(-fcolor-diagnostics CLANG_COLOR)
check_cxx_compiler_flag(-Wno-deprecated-enum-enum-conversion ENUM_ENUM_WARNING)

if (GCC_COLOR)
  add_compile_options(-fdiagnostics-color=always)
endif()
if (CLANG_COLOR)
  add_compile_options(-fcolor-diagnostics)
endif()

if(ENUM_ENUM_WARNING)
  add_compile_options(-Wno-deprecated-enum-enum-conversion)
endif()

if(ENABLE_WERROR OR ENABLE_STRICT_WERROR)
  add_compile_options(-Werror)
  if (NOT ENABLE_STRICT_WERROR)
    # Disable some Werror that can add frustration when developing
    add_compile_options(-Wno-error=unused-variable)
  endif()
endif()

if (NOT TUNE_ARCH STREQUAL "generic")
  check_cxx_compiler_flag("-march=${TUNE_ARCH}" COMPILER_SUPPORTS_ARCH_TYPE)
  if(COMPILER_SUPPORTS_ARCH_TYPE)
    add_compile_options("-march=${TUNE_ARCH}")
  else()
    message(FATAL_ERROR "Trying to compile arch type '${TUNE_ARCH}' but the compiler doesn't support this")
  endif()
endif()

if (TUNE_CPU STREQUAL "native")
  if(_M_ARM_64)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 999999.0)
      # Clang 12.0 fixed the -mcpu=native bug with mixed big.little implementers
      # Clang can not currently check for native Apple M1 type in hypervisor. Currently disabled
      check_cxx_compiler_flag("-mcpu=native" COMPILER_SUPPORTS_CPU_TYPE)
      if(COMPILER_SUPPORTS_CPU_TYPE)
        add_compile_options("-mcpu=native")
      endif()
    else()
      # Due to an oversight in llvm, it declares any reasonably new Kryo CPU to only be ARMv8.0
      # Manually detect newer CPU revisions until clang and llvm fixes their bug
      # This script will either provide a supported CPU or 'native'
      # Additionally -march doesn't work under AArch64+Clang, so you have to use -mcpu or -mtune
      execute_process(COMMAND python3 "${PROJECT_SOURCE_DIR}/Scripts/aarch64_fit_native.py" "/proc/cpuinfo" "${CMAKE_CXX_COMPILER_VERSION}"
        OUTPUT_VARIABLE AARCH64_CPU)

      string(STRIP ${AARCH64_CPU} AARCH64_CPU)

      check_cxx_compiler_flag("-mcpu=${AARCH64_CPU}" COMPILER_SUPPORTS_CPU_TYPE)
      if(COMPILER_SUPPORTS_CPU_TYPE)
        add_compile_options("-mcpu=${AARCH64_CPU}")
      endif()
    endif()
  else()
    check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
    if(COMPILER_SUPPORTS_MARCH_NATIVE)
      add_compile_options("-march=native")
    endif()
  endif()
else()
  check_cxx_compiler_flag("-mcpu=${TUNE_CPU}" COMPILER_SUPPORTS_CPU_TYPE)
  if(COMPILER_SUPPORTS_CPU_TYPE)
    add_compile_options("-mcpu=${TUNE_CPU}")
  else()
    message(FATAL_ERROR "Trying to compile cpu type '${TUNE_CPU}' but the compiler doesn't support this")
  endif()
endif()

if (ENABLE_IWYU)
  find_program(IWYU_EXE "iwyu")
  if (IWYU_EXE)
    message(STATUS "IWYU enabled")
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_EXE}")
  endif()
endif()

if (ENABLE_CLANG_FORMAT)
  find_program(CLANG_TIDY_EXE "clang-tidy")
  if (NOT CLANG_TIDY_EXE)
    message(FATAL_ERROR "Couldn't find clang-tidy")
  endif()

  set(CLANG_TIDY_FLAGS
    "-checks=*"
    "-fuchsia*"
    "-bugprone-macro-parentheses"
    "-clang-analyzer-core.*"
    "-cppcoreguidelines-pro-type-*"
    "-cppcoreguidelines-pro-bounds-array-to-pointer-decay"
    "-cppcoreguidelines-pro-bounds-pointer-arithmetic"
    "-cppcoreguidelines-avoid-c-arrays"
    "-cppcoreguidelines-avoid-magic-numbers"
    "-cppcoreguidelines-pro-bounds-constant-array-index"
    "-cppcoreguidelines-no-malloc"
    "-cppcoreguidelines-special-member-functions"
    "-cppcoreguidelines-owning-memory"
    "-cppcoreguidelines-macro-usage"
    "-cppcoreguidelines-avoid-goto"
    "-google-readability-function-size"
    "-google-readability-namespace-comments"
    "-google-readability-braces-around-statements"
    "-google-build-using-namespace"
    "-hicpp-*"
    "-llvm-namespace-comment"
    "-llvm-include-order" # Messes up with case sensitivity
    "-llvmlibc-*"
    "-misc-unused-parameters"
    "-modernize-loop-convert"
    "-modernize-use-auto"
    "-modernize-avoid-c-arrays"
    "-modernize-use-nodiscard"
    "readability-*"
    "-readability-function-size"
    "-readability-implicit-bool-conversion"
    "-readability-braces-around-statements"
    "-readability-else-after-return"
    "-readability-magic-numbers"
    "-readability-named-parameter"
    "-readability-uppercase-literal-suffix"
    "-cert-err34-c"
    "-cert-err58-cpp"
    "-bugprone-exception-escape"
    )
  string(REPLACE ";" "," CLANG_TIDY_FLAGS "${CLANG_TIDY_FLAGS}")
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE} "${CLANG_TIDY_FLAGS}")
endif()

add_compile_options(-Wall)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Config.h.in
  ${CMAKE_BINARY_DIR}/generated/ConfigDefines.h)

if (BUILD_TESTS)
  include(CTest)
  enable_testing()
  message(STATUS "Unit tests are enabled")
endif()

add_subdirectory(FEXHeaderUtils/)
add_subdirectory(External/FEXCore)

# Binfmt_misc files must be installed prior to Source/ installs
add_subdirectory(Data/binfmts/)

add_subdirectory(Source/)
add_subdirectory(Data/AppConfig/)

# Install the ThunksDB file
file(GLOB CONFIG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Data/*.json)

# Any application configuration json file gets installed
foreach(CONFIG_SRC ${CONFIG_SOURCES})
  install(FILES ${CONFIG_SRC}
    DESTINATION ${DATA_DIRECTORY}/)
endforeach()

if (BUILD_TESTS)
  add_subdirectory(unittests/)
endif()

if (BUILD_THUNKS)
  set (FEX_PROJECT_SOURCE_DIR ${PROJECT_SOURCE_DIR})
  add_subdirectory(ThunkLibs/Generator)

  # Thunk targets for both host libraries and IDE integration
  add_subdirectory(ThunkLibs/HostLibs)

  # Thunk targets for IDE integration of guest code, only
  add_subdirectory(ThunkLibs/GuestLibs)

  # Thunk targets for guest libraries
  include(ExternalProject)
  ExternalProject_Add(guest-libs
    PREFIX guest-libs
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThunkLibs/GuestLibs"
    BINARY_DIR "Guest"
    CMAKE_ARGS
      "-DBITNESS=64"
      "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DENABLE_CLANG_THUNKS=${ENABLE_CLANG_THUNKS}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${X86_64_TOOLCHAIN_FILE}"
      "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
      "-DSTRUCT_VERIFIER=${CMAKE_SOURCE_DIR}/Scripts/StructPackVerifier.py"
      "-DFEX_PROJECT_SOURCE_DIR=${FEX_PROJECT_SOURCE_DIR}"
      "-DGENERATOR_EXE=$<TARGET_FILE:thunkgen>"
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON
    DEPENDS thunkgen
  )

  ExternalProject_Add(guest-libs-32
    PREFIX guest-libs-32
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThunkLibs/GuestLibs"
    BINARY_DIR "Guest_32"
    CMAKE_ARGS
      "-DBITNESS=32"
      "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DENABLE_CLANG_THUNKS=${ENABLE_CLANG_THUNKS}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${X86_32_TOOLCHAIN_FILE}"
      "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
      "-DSTRUCT_VERIFIER=${CMAKE_SOURCE_DIR}/Scripts/StructPackVerifier.py"
      "-DFEX_PROJECT_SOURCE_DIR=${FEX_PROJECT_SOURCE_DIR}"
      "-DGENERATOR_EXE=$<TARGET_FILE:thunkgen>"
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON
    DEPENDS thunkgen
  )

  install(
    CODE "MESSAGE(\"-- Installing: guest-libs\")"
    CODE "
    EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} --build . --target install
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Guest
    )"
    DEPENDS guest-libs
  )

  install(
    CODE "MESSAGE(\"-- Installing: guest-libs-32\")"
    CODE "
    EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} --build . --target install
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Guest_32
    )"
    DEPENDS guest-libs-32
  )

  add_custom_target(uninstall_guest-libs
    COMMAND ${CMAKE_COMMAND} "--build" "." "--target" "uninstall"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Guest
  )

  add_custom_target(uninstall_guest-libs-32
    COMMAND ${CMAKE_COMMAND} "--build" "." "--target" "uninstall"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Guest_32
  )

  add_dependencies(uninstall uninstall_guest-libs)
  add_dependencies(uninstall uninstall_guest-libs-32)
endif()

set(FEX_VERSION_MAJOR "0")
set(FEX_VERSION_MINOR "0")
set(FEX_VERSION_PATCH "0")

if (OVERRIDE_VERSION STREQUAL "detect")
  find_package(Git)
  if (GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --abbrev=0
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE GIT_DESCRIBE_STRING
      RESULT_VARIABLE GIT_ERROR
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if (NOT ${GIT_ERROR} EQUAL 0)
      # Likely built in a way that doesn't have tags
      # Setup a version tag that is unknown
      set(GIT_DESCRIBE_STRING "FEX-0000")
    endif()
  endif()
else()
  set(GIT_DESCRIBE_STRING "FEX-${OVERRIDE_VERSION}")
endif()

# Parse the version here
# Change something like `FEX-2106.1-76-<hash>` in to a list
string(REPLACE "-" ";" DESCRIBE_LIST ${GIT_DESCRIBE_STRING})

# Extract the `2106.1` element
list(GET DESCRIBE_LIST 1 DESCRIBE_LIST)

# Change `2106.1` in to a list
string(REPLACE "." ";" DESCRIBE_LIST ${DESCRIBE_LIST})

# Calculate list size
list(LENGTH DESCRIBE_LIST LIST_SIZE)

# Pull out the major version
list(GET DESCRIBE_LIST 0 FEX_VERSION_MAJOR)

# Minor version only exists if there is a .1 at the end
# eg: 2106 versus 2106.1
if (LIST_SIZE GREATER 1)
  list(GET DESCRIBE_LIST 1 FEX_VERSION_MINOR)
endif()

# Package creation
set (CPACK_GENERATOR "DEB")
set (CPACK_PACKAGE_NAME fex-emu)
set (CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${GIT_DESCRIBE_STRING}_${CMAKE_SYSTEM_PROCESSOR}")
set (CPACK_PACKAGE_CONTACT "FEX-Emu Maintainers <team@fex-emu.com>")
set (CPACK_PACKAGE_VERSION_MAJOR "${FEX_VERSION_MAJOR}")
set (CPACK_PACKAGE_VERSION_MINOR "${FEX_VERSION_MINOR}")
set (CPACK_PACKAGE_VERSION_PATCH "${FEX_VERSION_PATCH}")
set (CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/CPack/Description.txt")

# Debian defines
set (CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libepoxy0, libsdl2-2.0-0, libegl1, libx11-6, squashfuse")
set (CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_SOURCE_DIR}/CPack/postinst;${CMAKE_CURRENT_SOURCE_DIR}/CPack/prerm;${CMAKE_CURRENT_SOURCE_DIR}/CPack/triggers")
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  # binfmt_misc conflicts with qemu-user-static
  # We also only install binfmt_misc on aarch64 hosts
  set (CPACK_DEBIAN_PACKAGE_CONFLICTS "${CPACK_DEBIAN_PACKAGE_CONFLICTS}, qemu-user-static")
endif()
include (CPack)

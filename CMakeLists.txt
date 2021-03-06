CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(inihck VERSION 0.0.1 LANGUAGES C)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/CMake")

# Subprojects
include(subproject)
add_subdirectory(lib)

# CPack
set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "7Z")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pkg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Small ini parser")
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION ON)

# Includes
include(GNUInstallDirs)
include(FeatureSummary)
include(CPack)
include(CTest)
include(test)

# Options
OPTION(INIHCK_BUILD_STATIC "Build inihck as static library" OFF)
OPTION(INIHCK_BUILD_TESTS "Build inihck tests" ON)

add_feature_info(Static INIHCK_BUILD_STATIC "Compile as static library")
add_feature_info(Tests INIHCK_BUILD_TESTS "Compile tests")

if (NOT INIHCK_BUILD_STATIC)
   set(BUILD_SHARED_LIBS ON)
endif ()

if (MINGW)
   set(BUILD_SHARED_LIBS OFF)
endif ()

set(CTEST_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CTEST_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)

# Compiler options
include(GCCCompatibleCompilerOptions)

if (MINGW)
   set(ldflags -O1 --sort-common --as-needed -static)
   set(cflags -flto -fuse-linker-plugin)
   add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
elseif (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(ldflags -O1 --sort-common --as-needed -z,relro -z,now)
   set(cflags -flto -fuse-linker-plugin)
endif ()

check_c_compiler_flag(-fstack-protector-strong has_fstack_protector_strong)
if (has_fstack_protector_strong)
   list(APPEND cflags -fstack-protector-strong -fstack-check --param ssp-buffer-size=4)
else ()
   list(APPEND cflags -fstack-protector-all -fstack-check --param ssp-buffer-size=4)
endif ()

create_custom_linker_flags(Upstream ${ldflags})
create_custom_compiler_flags(Upstream -g -O2 ${cflags})

add_compiler_warnings(-Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal -Wcast-align -Wpointer-arith -Wmissing-prototypes)

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   add_compiler_warnings(-Wsuggest-attribute=pure -Wsuggest-attribute=const)
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang")
   add_compiler_warnings(-Wno-pointer-bool-conversion -Wno-missing-field-initializers -Wno-missing-braces)
endif ()

# -std=c99 -fpic -fpie -D_DEFAULT_SOURCE
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_definitions(-D_DEFAULT_SOURCE)

add_subdirectory(src)

if ("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
   feature_summary(WHAT ALL)
endif ()

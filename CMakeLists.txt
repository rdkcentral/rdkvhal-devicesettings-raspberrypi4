cmake_minimum_required(VERSION 3.10)

# Project name
project(ds-hal VERSION 0.0 LANGUAGES C CXX)

# Set the library name
set(LIBNAME ds-hal)
set(LIBNAMEFULL lib${LIBNAME}.so)
set(DSHAL_API_MAJOR_VERSION 0)
set(DSHAL_API_MINOR_VERSION 0)
set(VERSION ${DSHAL_API_MAJOR_VERSION}.${DSHAL_API_MINOR_VERSION})
set(LIBSOM ${LIBNAMEFULL}.${DSHAL_API_MAJOR_VERSION})
set(LIBSOV ${LIBNAMEFULL}.${VERSION})

# Source files
file(GLOB SOURCES "*.c")

# Include directories
include_directories(/usr/include/interface/vmcs_host/linux)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -g -fPIC -D_REENTRANT -Wall -DALSA_AUDIO_MASTER_CONTROL_ENABLE")

# Create the shared library
add_library(${LIBNAME} SHARED ${SOURCES})
target_link_libraries(${LIBNAME} vchostif vchiq_arm vcos asound)

# Change resolution executable
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)

if (NOT LIBDRM_FOUND)
    message(FATAL_ERROR "libdrm not found!")
endif()

include_directories(${LIBDRM_INCLUDE_DIRS})
link_directories(${LIBDRM_LIBRARY_DIRS})
add_definitions(${LIBDRM_CFLAGS_OTHER})

add_executable(change_resolutions_test change_resolution.c)
target_link_libraries(change_resolutions_test ${LIBDRM_LIBRARIES})

# Installation
install(TARGETS ${LIBNAME} LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX})
install(TARGETS change_resolutions_test RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

# Clean target
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${CMAKE_BINARY_DIR}/*.so*;${CMAKE_BINARY_DIR}/*.o;${CMAKE_BINARY_DIR}/change_resolutions_test")

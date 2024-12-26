cmake_minimum_required(VERSION 3.10)

project(ds-hal VERSION 0.0 LANGUAGES C CXX)

# Set the library name
set(LIBNAME ds-hal)

set(DEFAULT_BUILD_TYPE "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(GNUInstallDirs)

# All source files
file(GLOB SOURCES "*.c")

include_directories(/usr/include/interface/vmcs_host/linux)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -g -fPIC -D_REENTRANT -Wall -DALSA_AUDIO_MASTER_CONTROL_ENABLE")

# Create the shared library
add_library(${LIBNAME} SHARED ${SOURCES})
set_target_properties(${LIBNAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION}
)
target_link_libraries(${LIBNAME} vchostif vchiq_arm vcos asound)

# Target executable
set(BINNAME change_resolutions_test)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)

if (NOT LIBDRM_FOUND)
    message(FATAL_ERROR "libdrm not found!")
endif()

include_directories(${LIBDRM_INCLUDE_DIRS})
link_directories(${LIBDRM_LIBRARY_DIRS})
add_definitions(${LIBDRM_CFLAGS_OTHER})

add_executable(${BINNAME} change_resolution.c)
target_link_libraries(${BINNAME} ${LIBDRM_LIBRARIES})

# Installation
install(TARGETS ${LIBNAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ${BINNAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Clean target
add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake Makefile
)
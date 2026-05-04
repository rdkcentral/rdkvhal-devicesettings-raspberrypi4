cmake_minimum_required(VERSION 3.10)

# Middleware tries to load libds-hal.so.0 (RDK_DSHAL_NAME [libds-hal.so.0])
project(ds-hal VERSION 0.0.0 LANGUAGES C)

# Set the library name
set(LIBNAME "dshal" CACHE STRING "Name of the HAL library")
option(ENABLE_FPD_MULTI_PROCESS_GUARD "Enable inter-process LED ownership guard" OFF)
option(ENABLE_DSHAL_SINGLETON_GUARD "Enable process-wide singleton guard for dshal library" ON)

set(DEFAULT_BUILD_TYPE "Release")

set(DRI_CARD "/dev/dri/card0" CACHE STRING "Path to the dri device")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBDRM REQUIRED libdrm)
pkg_check_modules(LIBUDEV REQUIRED libudev)

# All source files
file(GLOB ALL_SOURCES "*.c")
set(SOURCES ${ALL_SOURCES})

# add DRI_CARD to the compile flags
if (DRI_CARD)
	message(STATUS "DRI_CARD is set to ${DRI_CARD}")
	add_definitions(-DDRI_CARD=\"${DRI_CARD}\")
else()
	message(FATAL_ERROR "DRI_CARD is not set")
endif()

if (ENABLE_FPD_MULTI_PROCESS_GUARD)
	message(STATUS "ENABLE_FPD_MULTI_PROCESS_GUARD is ON")
	add_definitions(-DDSFPD_ENABLE_MULTI_PROCESS_GUARD)
else()
	message(STATUS "ENABLE_FPD_MULTI_PROCESS_GUARD is OFF")
endif()

if (ENABLE_DSHAL_SINGLETON_GUARD)
	message(STATUS "ENABLE_DSHAL_SINGLETON_GUARD is ON")
	add_definitions(-DDSHAL_ENABLE_SINGLETON_GUARD)
else()
	message(STATUS "ENABLE_DSHAL_SINGLETON_GUARD is OFF")
endif()

# Add GIT version to the compile flags
execute_process(
	COMMAND git rev-parse --short HEAD
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (NOT GIT_SHA STREQUAL "")
	message(STATUS "GIT_SHA is set to ${GIT_SHA}")
	add_definitions(-DGIT_SHA=\"${GIT_SHA}\")
else()
	message(STATUS "GIT_SHA is not set")
endif()

# Compiler flags , add security flags as well
set(SECURITY_CFLAGS "-fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -fPIE -pie -Wl,-z,relro -Wl,-z,now")
# Set compiler flags, including the security flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -g -fPIC -D_REENTRANT -Werror -Wall -Wextra ${SECURITY_CFLAGS}")

# Create the shared library
add_library(${LIBNAME} SHARED ${SOURCES})
set_target_properties(${LIBNAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(${LIBNAME} PRIVATE
	${LIBDRM_INCLUDE_DIRS}
	${LIBUDEV_INCLUDE_DIRS}
)
target_compile_options(${LIBNAME} PRIVATE
	${LIBDRM_CFLAGS_OTHER}
	${LIBUDEV_CFLAGS_OTHER}
)
target_compile_definitions(${LIBNAME} PRIVATE DS_HAL_EXPORT_CONFIG_SYMBOLS DSHAL_ENABLE_ALSA_EXPERIMENTAL)
target_link_directories(${LIBNAME} PRIVATE
	${LIBDRM_LIBRARY_DIRS}
	${LIBUDEV_LIBRARY_DIRS}
)
target_link_libraries(${LIBNAME} asound pthread m
	${LIBDRM_LIBRARIES}
	${LIBUDEV_LIBRARIES}
)

# Installation
install(TARGETS ${LIBNAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
# Install headers ending with *Settings.h
file(GLOB SETTINGS_HEADERS
	"${CMAKE_SOURCE_DIR}/dsAudioSettings.h"
	"${CMAKE_SOURCE_DIR}/dsFPDSettings.h"
	"${CMAKE_SOURCE_DIR}/dsVideoDeviceSettings.h"
	"${CMAKE_SOURCE_DIR}/dsVideoPortSettings.h"
	"${CMAKE_SOURCE_DIR}/dsVideoResolutionSettings.h"
)
install(FILES ${SETTINGS_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rdk/halif/ds-hal)

# Install ALSA default config for HDMI audio routing
install(FILES ${CMAKE_SOURCE_DIR}/config/asound.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})

# Install systemd unit required for runtime audio softvol initialization.
# Note: service enablement should be handled by the target package or deployment flow.
install(FILES ${CMAKE_SOURCE_DIR}/systemd/rpiAudioSoftvol.service
	DESTINATION /lib/systemd/system)

# Clean target
add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake Makefile
)

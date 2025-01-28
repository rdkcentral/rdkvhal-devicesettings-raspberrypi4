cmake_minimum_required(VERSION 3.10)

project(ds-hal VERSION 0.0 LANGUAGES C CXX)

# Set the library name
set(LIBNAME ds-hal)

set(DEFAULT_BUILD_TYPE "Release")

option(DRI_CARD "Path to the dri device" "/dev/dri/card0")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(GNUInstallDirs)

# All source files
file(GLOB SOURCES "*.c")

include_directories(/usr/include/interface/vmcs_host/linux)

# add DRI_CARD to the compile flags
if (DRI_CARD)
	message(STATUS "DRI_CARD is set to ${DRI_CARD}")
	add_definitions(-DDRI_CARD=\"${DRI_CARD}\")
else()
	message(FATAL_ERROR "DRI_CARD is not set")
endif()

# Compiler flags , add security flags as well
set(SECURITY_CFLAGS "-fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -fPIE -pie -Wl,-z,relro -Wl,-z,now")
# Set C++ compiler flags, including the security flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -g -fPIC -D_REENTRANT -Werror -Wall -Wextra -DALSA_AUDIO_MASTER_CONTROL_ENABLE ${SECURITY_CFLAGS}")

# Create the shared library
add_library(${LIBNAME} SHARED ${SOURCES})
set_target_properties(${LIBNAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION}
)
target_link_libraries(${LIBNAME} vchostif vchiq_arm vcos asound)

# Installation
install(TARGETS ${LIBNAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Clean target
add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake Makefile
)

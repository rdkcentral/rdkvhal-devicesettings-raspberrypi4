cmake_minimum_required(VERSION 3.10)

# Middleware tries to load libds-hal.so.0 (RDK_DSHAL_NAME [libds-hal.so.0])
project(ds-hal VERSION 0.0.0 LANGUAGES C)

# Set the library name
set(LIBNAME "dshal" CACHE STRING "Name of the HAL library")
set(DAEMON_NAME "dshal-tvsvc-daemon" CACHE STRING "Name of the TVService broker daemon executable")
option(ENABLE_FPD_MULTI_PROCESS_GUARD "Enable inter-process LED ownership guard" OFF)
option(ENABLE_DSHAL_SINGLETON_GUARD "Enable process-wide singleton guard for dshal library" ON)

set(DEFAULT_BUILD_TYPE "Release")

option(DRI_CARD "Path to the dri device" "/dev/dri/card0")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(GNUInstallDirs)

# All source files — exclude the daemon executable source from the shared library
file(GLOB ALL_SOURCES "*.c")
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_SOURCE_DIR}/dsTVSvcDaemon.c")
set(SOURCES ${ALL_SOURCES})

include_directories(/usr/include/interface/vmcs_host/linux)

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
set(SECURITY_LDFLAGS "-pie -Wl,-z,relro -Wl,-z,now")
separate_arguments(SECURITY_CFLAGS_LIST NATIVE_COMMAND "${SECURITY_CFLAGS}")
separate_arguments(SECURITY_LDFLAGS_LIST NATIVE_COMMAND "${SECURITY_LDFLAGS}")
# Set compiler flags, including the security flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -g -fPIC -D_REENTRANT -Werror -Wall -Wextra ${SECURITY_CFLAGS}")

# Create the shared library
add_library(${LIBNAME} SHARED ${SOURCES})
set_target_properties(${LIBNAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_link_libraries(${LIBNAME} asound pthread m)

# TVService broker daemon — separate executable, sole owner of VCHI
add_executable(${DAEMON_NAME}
	dsTVSvcDaemon.c
	dshalLogger.c
)
target_include_directories(${DAEMON_NAME} PRIVATE
	${CMAKE_SOURCE_DIR}
	/usr/include/interface/vmcs_host/linux
)
target_compile_options(${DAEMON_NAME} PRIVATE
	-std=c11 -g -fPIE -D_REENTRANT -Werror -Wall -Wextra
	${SECURITY_CFLAGS_LIST}
)
target_link_options(${DAEMON_NAME} PRIVATE
	${SECURITY_LDFLAGS_LIST}
)
target_link_libraries(${DAEMON_NAME} vchostif vchiq_arm vcos pthread)

# Installation
install(TARGETS ${LIBNAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Clean target
add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake Makefile
)

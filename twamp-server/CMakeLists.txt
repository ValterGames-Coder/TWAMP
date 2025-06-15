cmake_minimum_required(VERSION 3.12)
project(twampd VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Check for required headers
include(CheckIncludeFileCXX)
check_include_file_cxx("sys/socket.h" HAVE_SYS_SOCKET_H)
check_include_file_cxx("netinet/in.h" HAVE_NETINET_IN_H)
check_include_file_cxx("arpa/inet.h" HAVE_ARPA_INET_H)
check_include_file_cxx("syslog.h" HAVE_SYSLOG_H)

if(NOT HAVE_SYS_SOCKET_H OR NOT HAVE_NETINET_IN_H OR NOT HAVE_ARPA_INET_H OR NOT HAVE_SYSLOG_H)
    message(FATAL_ERROR "Required system headers not found")
endif()

# Source files
set(SOURCES
    main.cpp
)

# Create executable
add_executable(twampd ${SOURCES})

# Link libraries
target_link_libraries(twampd 
    PRIVATE 
    Threads::Threads
)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(twampd PRIVATE -pthread)
    target_link_options(twampd PRIVATE -pthread)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(twampd PRIVATE -pthread)
    target_link_options(twampd PRIVATE -pthread)
endif()

# Installation
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix")

install(TARGETS twampd
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# Install configuration files
#install(FILES twampd.conf
#    DESTINATION etc
#    COMPONENT Configuration
#    OPTIONAL
#)

# Install systemd service file
#install(FILES twampd.service
#    DESTINATION lib/systemd/system
#    COMPONENT SystemdService
#    OPTIONAL
#)

# Install manual page
#install(FILES twampd.8
#    DESTINATION share/man/man8
#    COMPONENT Documentation
#    OPTIONAL
#)

# Create directories for runtime files
#install(DIRECTORY DESTINATION var/log/twampd
#    COMPONENT Runtime
#)

#install(DIRECTORY DESTINATION var/run/twampd
#    COMPONENT Runtime
#)

# Package configuration
set(CPACK_PACKAGE_NAME "twampd")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "TWAMP Daemon - RFC 5357 compliant Two-Way Active Measurement Protocol server")
set(CPACK_PACKAGE_CONTACT "admin@example.com")
set(CPACK_PACKAGE_VENDOR "TWAMP Project")

# DEB package specific settings
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libstdc++6 (>= 5.4.0)")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "TWAMP Maintainer <admin@example.com>")

# RPM package specific settings
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Daemons")
set(CPACK_RPM_PACKAGE_LICENSE "GPL")
set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.17, libstdc++ >= 5.4.0")

include(CPack)

# Custom targets for development
add_custom_target(format
    COMMAND clang-format -i ${SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Formatting source code"
)

add_custom_target(install-service
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/twampd.service /lib/systemd/system/
    COMMAND systemctl daemon-reload
    COMMENT "Installing systemd service"
    DEPENDS twampd
)

add_custom_target(uninstall-service
    COMMAND systemctl stop twampd || true
    COMMAND systemctl disable twampd || true
    COMMAND ${CMAKE_COMMAND} -E remove /lib/systemd/system/twampd.service
    COMMAND systemctl daemon-reload
    COMMENT "Uninstalling systemd service"
)

# Debug information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

# Print configuration summary
message(STATUS "")
message(STATUS "TWAMP Daemon Configuration Summary:")
message(STATUS "===================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

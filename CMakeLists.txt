cmake_minimum_required(VERSION 2.6)
project(dnds)

SET(CPACK_PACKAGE_VERSION "0.5.2")

enable_testing()

# Using Hardening Options
# https://wiki.debian.org/Hardening
set(HARDENING OPTIONS_COMMON
    -W
    -Wall
    -fstack-protector-all
    --param ssp-buffer-size=4
    -Wformat
    -Wformat-security
    -Werror=format-security
    -D_FORTIFY_SOURCE=2
)

if (WIN32)
    set(compiler_options "${HARDENING_OPTIONS_COMMON} -fPIE -O1")
elseif(APPLE)
    set(MACOS_10_6_FLAG "-stdlib=libstdc++")
    set(CMAKE_OSX_SYSROOT "/SDKs/MacOSX10.6.sdk/")
    set(compiler_options "${HARDENING_OPTIONS_COMMON} -fPIE ${MACOS_10_6_FLAG} -O1")
elseif(UNIX)
    set(HARDENING_OPTIONS_LINUX
        ${HARDENING_OPTIONS_COMMON}
        -Wl,-z,relro
    )
    set(compiler_options "${HARDENING_OPTIONS_LINUX} -O1")
endif()

if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

if(APPLE)
	set(apple_package dnc.app) #DynVPN_Client.app)
	set(library_dest_dir ${apple_package}/Contents/MacOS)
	set(qtconf_dest_dir ${apple_package}/Contents/Resources)
else()
	set(library_dest_dir lib)
endif()

add_subdirectory(libdnds)

option (WITH_DNC "WITH_DNC" ON)

if (WITH_DNC)
	add_subdirectory(dnc)
endif()

option (WITH_DSD "WITH_DND" OFF)
option (WITH_DSD "WITH_DSD" OFF)

if (WITH_DSD)
	add_subdirectory(dsd)
endif()

if (WITH_DND)
	add_subdirectory(dnd)
endif()

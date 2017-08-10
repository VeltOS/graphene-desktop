# This file is part of graphene-desktop, the desktop environment of VeltOS.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the WTFPL.

cmake_minimum_required(VERSION 3.4)
project(graphene-desktop)

set(GRAPHENE_VERSION 0.3)
set(GRAPHENE_DEBUG TRUE)

if(GRAPHENE_DEBUG)
	set(CMAKE_BUILD_TYPE DEBUG)
	add_definitions(-DGRAPHENE_DEBUG)
endif()

add_definitions(-DGRAPHENE_VERSION_STR=\"${GRAPHENE_VERSION}\")

# Assume the user wants to install Graphene to /usr unless otherwise specified
# with the -DCMAKE_INSTALL_PREFIX=<path> flag to cmake. http://stackoverflow.com/a/16076855
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

# Config
include_directories(${PROJECT_BINARY_DIR})

# Acquire libraries needed for all targets
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0>=2.10)
link_libraries(${GLIB2_LIBRARIES})
include_directories(${GLIB2_INCLUDE_DIRS})

# Setup targets
add_subdirectory(src)

# Install
install(FILES graphene.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/xsessions)
install(DIRECTORY schemas/ DESTINATION ${CMAKE_INSTALL_PREFIX}/share/glib-2.0/schemas)

if(NOT DONT_COMPILE_SCHEMAS)
  install(CODE "execute_process(COMMAND make compile-schemas)") # There is probably a better way to do this...
endif()

# Convenience target to compile glib schemas (usually post-install and post-uninstall)
add_custom_target(compile-schemas COMMAND glib-compile-schemas \"${CMAKE_INSTALL_PREFIX}/share/glib-2.0/schemas\" )

# Force install - kills running process, runs install
add_custom_target(finstall COMMAND killall graphene-desktop nautilus-desktop gnome-settings-daemon\; make install )

# Uninstall target (only works after installing with install_manifest.txt)
# Removes all files listed in install_manifest.txt, and then removes install_manifest.txt
# If install_manifest.txt is not available, an error message is printed.
add_custom_target(uninstall COMMAND { [ -f install_manifest.txt ] && xargs --verbose -d '\\n' rm < install_manifest.txt && rm install_manifest.txt\; } || echo ' *** ERROR: Missing install_manifest.txt: you must install first!' )

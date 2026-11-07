cmake_minimum_required(VERSION 3.22)
project(microdroid_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# libsla is built out of tree from third_party/sleigh (see third_party/VERSIONS); its install
# rules require every processor spec, so link the static archive and headers directly.
set(MICRODROID_SLEIGH_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sleigh/build"
    CACHE PATH "Build tree of third_party/sleigh containing libsla.a and include/")

find_package(ZLIB REQUIRED)

add_library(sla STATIC IMPORTED)
set_target_properties(sla PROPERTIES
  IMPORTED_LOCATION "${MICRODROID_SLEIGH_BUILD}/libsla.a"
  INTERFACE_INCLUDE_DIRECTORIES "${MICRODROID_SLEIGH_BUILD}/include"
  INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)

add_executable(sla_probe tools/sla_probe.cpp)
target_link_libraries(sla_probe PRIVATE sla)

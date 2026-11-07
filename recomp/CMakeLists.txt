cmake_minimum_required(VERSION 3.22)
project(microdroid_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
add_library(rearm_elf elf/elf_image.cpp)
target_include_directories(rearm_elf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(elf_tests ../tests/elf/elf_image_test.cpp)
target_link_libraries(elf_tests PRIVATE rearm_elf)
add_test(NAME elf_loader_relocations COMMAND elf_tests)
get_filename_component(REARM_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
set_tests_properties(elf_loader_relocations PROPERTIES TIMEOUT 30
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${REARM_COMPILER_BIN}")
option(REARM_BUILD_SLEIGH_TOOLS "Build tools requiring the pinned SLEIGH dependency" ON)
if(REARM_BUILD_SLEIGH_TOOLS)

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
add_executable(rearm tools/rearm.cpp lift/lifter.cpp emit/emitter.cpp)
target_link_libraries(rearm PRIVATE rearm_elf sla)
add_library(rearm_ir ir/verify.cpp)
target_link_libraries(rearm_ir PUBLIC rearm_elf sla)
target_link_libraries(rearm PRIVATE rearm_ir)
add_executable(ir_tests ../tests/ir/verify_test.cpp)
target_link_libraries(ir_tests PRIVATE rearm_ir)
add_test(NAME normalized_ir_verifier COMMAND ir_tests)
set_tests_properties(normalized_ir_verifier PROPERTIES TIMEOUT 30
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${REARM_COMPILER_BIN}")
endif()

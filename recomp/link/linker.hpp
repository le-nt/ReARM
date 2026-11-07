// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "elf/elf_image.hpp"

namespace rearm::link {
struct ModuleInput {
  std::string name;
  std::filesystem::path path;
  std::uint32_t load_bias{};
};
struct ModuleInfo {
  std::string name;
  std::string soname;
  std::uint32_t load_bias{};
  std::vector<std::string> needed;
  std::vector<std::uint32_t> init_functions;
  std::vector<std::uint32_t> fini_functions;
};
struct LinkedImage {
  elf::Image image;
  std::vector<ModuleInfo> modules;
  // Ordered guest entry addresses only. The caller must execute these through
  // translated code, never by casting to host pointers.
  std::vector<std::uint32_t> constructors;
  std::vector<std::uint32_t> destructors;
};

// A bounded, eager, explicitly supplied module namespace. Module zero is main;
// input order establishes lookup precedence among strong definitions. The first
// weak definition is a fallback only when no strong definition exists. Only
// dynamic global/weak default/protected symbols are exported. Local, hidden,
// protected and DT_SYMBOLIC definitions remain bound inside their own module.
// DT_NEEDED names must match an input name/SONAME or an explicit external module.
// No filesystem search, host loader call, lazy binding, or runtime codegen occurs.
LinkedImage link(std::vector<ModuleInput> inputs,
                 const elf::Image::Resolver& external = {},
                 const std::vector<std::string>& external_modules = {});
}  // namespace rearm::link

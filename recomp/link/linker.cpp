// SPDX-License-Identifier: Apache-2.0
#include "linker.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace rearm::link {
namespace {
bool executable(const elf::Image& image, std::uint32_t tagged_address) {
  const auto address = tagged_address & ~1u;
  if (address % (tagged_address & 1 ? 2 : 4)) return false;
  for (const auto& segment : image.segments()) {
    if ((segment.flags & 1) && address >= segment.address && std::uint64_t(address) - segment.address < segment.bytes.size()) return true;
  }
  return false;
}
bool exported(const elf::Symbol& symbol) {
  return symbol.dynamic && symbol.defined() && !symbol.name.empty() &&
      (symbol.binding == 1 || symbol.binding == 2) &&
      (symbol.visibility == 0 || symbol.visibility == 3);
}
std::vector<std::uint32_t> initializers(const elf::Image& image, bool finalizing) {
  const auto& dynamic = image.dynamic();
  const auto first = finalizing ? dynamic.fini : dynamic.init;
  const auto array = finalizing ? dynamic.fini_array : dynamic.init_array;
  const auto size = finalizing ? dynamic.fini_array_size : dynamic.init_array_size;
  std::vector<std::uint32_t> result;
  auto append = [&](std::uint32_t value) {
    if (!value || value == 0xffffffffu) return;
    if (!executable(image, value)) throw elf::Error("Initializer/destructor target is not executable in its module");
    result.push_back(value);
  };
  if (!finalizing) append(first);
  if (finalizing) {
    for (auto offset = size; offset; offset -= 4) append(image.read32(array + offset - 4));
    append(first);
  } else {
    for (std::uint32_t offset = 0; offset < size; offset += 4) append(image.read32(array + offset));
  }
  return result;
}
}

LinkedImage link(std::vector<ModuleInput> inputs, const elf::Image::Resolver& external,
                 const std::vector<std::string>& external_modules) {
  if (inputs.empty() || inputs.size() > 128) throw elf::Error("ELF link requires 1..128 explicitly supplied modules");
  std::vector<elf::Image> images;
  LinkedImage result;
  std::map<std::string, std::size_t> aliases;
  const std::set<std::string> declared_external(external_modules.begin(), external_modules.end());
  std::uint64_t mapped_bytes = 0;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> mappings;
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    auto& input = inputs[index];
    if (input.name.empty()) input.name = input.path.filename().string();
    if (input.name.empty()) throw elf::Error("ELF module name must not be empty");
    try { images.push_back(elf::Image::load(input.path, input.load_bias)); }
    catch (const elf::Error& error) { throw elf::Error("Module '" + input.name + "': " + error.what()); }
    const auto& image = images.back();
    const auto& dynamic = image.dynamic();
    if (index && image.type() != 3) throw elf::Error("Dependency '" + input.name + "' must be ET_DYN");
    if (dynamic.symbol_versions) throw elf::Error("Module '" + input.name + "': symbol versions are not supported by the initial linker profile");
    if (dynamic.preinit_array) throw elf::Error("Module '" + input.name + "': preinit arrays are not supported by the initial linker profile");
    for (const auto& symbol : image.symbols()) if (symbol.dynamic) {
      if (symbol.type == 6 || symbol.type == 10 || symbol.binding == 10)
        throw elf::Error("Module '" + input.name + "': unsupported TLS/IFUNC/unique symbol '" + symbol.name + "'");
      if (!symbol.defined() && symbol.visibility != 0 && !symbol.name.empty())
        throw elf::Error("Module '" + input.name + "': undefined nondefault-visible symbol '" + symbol.name + "'");
    }
    auto add_alias = [&](const std::string& alias) {
      if (alias.empty()) return;
      const auto [existing, inserted] = aliases.emplace(alias, index);
      if (!inserted && existing->second != index) throw elf::Error("Duplicate ELF module name/SONAME '" + alias + "'");
    };
    add_alias(input.name); add_alias(dynamic.soname);
    for (const auto& segment : image.segments()) {
      mapped_bytes += segment.bytes.size();
      if (mapped_bytes > 512ull * 1024 * 1024) throw elf::Error("ELF linked namespace exceeds 512 MiB mapped-byte limit");
      mappings.emplace_back(segment.address, std::uint64_t(segment.address) + segment.bytes.size());
    }
    result.modules.push_back({input.name, dynamic.soname, input.load_bias, dynamic.needed, {}, {}});
  }
  std::sort(mappings.begin(), mappings.end());
  for (std::size_t i = 1; i < mappings.size(); ++i) if (mappings[i - 1].second > mappings[i].first)
    throw elf::Error("Overlapping ELF module mappings; choose disjoint load biases");
  std::vector<std::vector<std::size_t>> dependencies(inputs.size());
  for (std::size_t index = 0; index < images.size(); ++index) {
    for (const auto& needed : images[index].dynamic().needed) {
      const auto module = aliases.find(needed);
      if (module != aliases.end()) dependencies[index].push_back(module->second);
      else if (!declared_external.contains(needed))
        throw elf::Error("Module '" + inputs[index].name + "' requires missing DT_NEEDED '" + needed + "'");
    }
  }
  struct Definition { std::uint32_t address; std::uint8_t type; std::uint8_t binding; };
  std::map<std::string, std::vector<Definition>> definitions;
  for (const auto& image : images) for (const auto& symbol : image.symbols()) if (exported(symbol))
    definitions[symbol.name].push_back({symbol.value, symbol.type, symbol.binding});
  auto resolve = [&](const elf::Symbol& requested) -> std::optional<std::uint32_t> {
    if (const auto found = definitions.find(requested.name); found != definitions.end()) {
      const Definition* weak = nullptr;
      for (const auto& definition : found->second) {
        if (requested.type && definition.type && requested.type != definition.type)
          throw elf::Error("ELF symbol type mismatch for '" + requested.name + "'");
        if (definition.binding == 1) return definition.address;
        if (!weak) weak = &definition;
      }
      if (weak) return weak->address;
    }
    return external ? external(requested) : std::nullopt;
  };
  for (std::size_t index = 0; index < images.size(); ++index) {
    try { images[index].relocate(resolve, true); }
    catch (const elf::Error& error) { throw elf::Error("Module '" + inputs[index].name + "': " + error.what()); }
    result.modules[index].init_functions = initializers(images[index], false);
    result.modules[index].fini_functions = initializers(images[index], true);
  }
  // Dependencies initialize before their owners. Cycles are traversed once in
  // deterministic input/DT_NEEDED order; no constructor executes during linking.
  std::vector<std::uint8_t> visited(images.size());
  std::vector<std::size_t> order;
  std::function<void(std::size_t)> visit = [&](std::size_t index) {
    if (visited[index]) return;
    visited[index] = 1;
    for (const auto dependency : dependencies[index]) visit(dependency);
    visited[index] = 2;
    order.push_back(index);
  };
  for (std::size_t index = 0; index < images.size(); ++index) visit(index);
  for (const auto index : order) {
    const auto& functions = result.modules[index].init_functions;
    result.constructors.insert(result.constructors.end(), functions.begin(), functions.end());
  }
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const auto& functions = result.modules[*it].fini_functions;
    result.destructors.insert(result.destructors.end(), functions.begin(), functions.end());
  }
  result.image = elf::Image::combine(std::move(images));
  return result;
}
}  // namespace rearm::link

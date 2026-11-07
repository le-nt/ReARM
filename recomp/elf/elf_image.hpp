// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rearm::elf {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Segment {
  std::uint32_t address{};
  std::uint32_t flags{};
  std::vector<std::uint8_t> bytes;
};

struct Symbol {
  std::string name;
  std::uint32_t value{};
  std::uint32_t size{};
  std::uint8_t binding{};
  std::uint8_t type{};
  std::uint16_t section{};
  bool defined() const { return section != 0; }
};

struct Relocation {
  std::uint32_t address{};
  std::uint32_t type{};
  std::uint32_t symbol{};
};

// Guest virtual addresses are never host pointers. PT_LOAD storage is sparse,
// including only mapped bytes; mapping a high address does not allocate its gap.
class Image {
 public:
  using Resolver = std::function<std::optional<std::uint32_t>(const Symbol&)>;
  static Image load(const std::filesystem::path& path, std::uint32_t load_bias = 0);
  static Image parse(std::span<const std::uint8_t> file, std::uint32_t load_bias = 0);

  std::uint32_t entry() const { return entry_; }
  std::uint32_t load_bias() const { return bias_; }
  std::uint16_t type() const { return type_; }
  const std::vector<Segment>& segments() const { return segments_; }
  const std::vector<Symbol>& symbols() const { return symbols_; }
  const std::vector<Relocation>& relocations() const { return relocations_; }
  std::optional<Symbol> find_symbol(std::string_view name) const;
  bool mapped(std::uint32_t address, std::size_t size = 1) const;
  std::vector<std::uint8_t> read(std::uint32_t address, std::size_t size) const;
  std::uint8_t read8(std::uint32_t address) const;
  std::uint16_t read16(std::uint32_t address) const;
  std::uint32_t read32(std::uint32_t address) const;
  void write32(std::uint32_t address, std::uint32_t value);

  // Supports ELF32 REL NONE, ABS32, REL32, GLOB_DAT, JUMP_SLOT, RELATIVE.
  // Validates and resolves the whole batch before changing memory. A second
  // application is rejected because REL addends reside in the target bytes.
  void relocate(const Resolver& resolver = {});

 private:
  const Segment& segment_at(std::uint32_t address) const;
  Segment& segment_at(std::uint32_t address);
  std::uint32_t bias_{};
  std::uint32_t entry_{};
  std::uint16_t type_{};
  bool relocated_{};
  std::vector<Segment> segments_;
  std::vector<Symbol> symbols_;
  std::vector<Relocation> relocations_;
};

}  // namespace rearm::elf

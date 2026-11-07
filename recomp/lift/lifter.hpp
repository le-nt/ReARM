// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "elf/elf_image.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rearm::lift {
struct Varnode {
  std::string space;
  std::uint64_t offset{};
  std::uint32_t size{};
};
struct Op {
  int opcode{};
  std::optional<Varnode> output;
  std::vector<Varnode> inputs;
  std::string userop;
};
struct Instruction {
  std::uint32_t address{};
  bool thumb{};
  std::uint32_t length{};
  std::vector<Op> ops;
};
struct Program {
  std::vector<Instruction> instructions;
  std::map<std::string, Varnode> registers;
  std::vector<std::string> userops;
};
// The entry address carries the ARM ELF Thumb tag in bit zero. Discovery follows
// direct p-code edges and exported STT_FUNC seeds, bounded to executable mappings.
// Dynamic destinations remain runtime dispatcher checks; no speculative sweep.
Program lift(const elf::Image& image, const std::filesystem::path& sla_path,
             std::uint32_t entry, std::uint32_t max_instructions = 100000);
}  // namespace rearm::lift

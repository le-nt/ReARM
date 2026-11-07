// SPDX-License-Identifier: Apache-2.0
#include "verify.hpp"
#include <ghidra/opcodes.hh>

#include <algorithm>
#include <bit>
#include <deque>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace rearm::ir {
namespace {
using namespace ghidra;
using lift::Instruction;
using lift::Op;
using lift::Varnode;
using Bytes = std::set<std::uint64_t>;
struct Shape { const char* name; std::size_t inputs; bool output; };
Shape shape(int opcode) {
#define SHAPE(op, count, out) case op: return {#op, count, out}
  switch (opcode) {
    SHAPE(CPUI_COPY, 1, true); SHAPE(CPUI_CAST, 1, true);
    SHAPE(CPUI_INT_ZEXT, 1, true); SHAPE(CPUI_INT_SEXT, 1, true);
    SHAPE(CPUI_LOAD, 2, true); SHAPE(CPUI_STORE, 3, false);
    SHAPE(CPUI_INT_ADD, 2, true); SHAPE(CPUI_INT_SUB, 2, true);
    SHAPE(CPUI_INT_MULT, 2, true); SHAPE(CPUI_INT_DIV, 2, true);
    SHAPE(CPUI_INT_SDIV, 2, true); SHAPE(CPUI_INT_REM, 2, true);
    SHAPE(CPUI_INT_SREM, 2, true); SHAPE(CPUI_INT_AND, 2, true);
    SHAPE(CPUI_INT_OR, 2, true); SHAPE(CPUI_INT_XOR, 2, true);
    SHAPE(CPUI_INT_NEGATE, 1, true); SHAPE(CPUI_INT_2COMP, 1, true);
    SHAPE(CPUI_BOOL_NEGATE, 1, true); SHAPE(CPUI_BOOL_AND, 2, true);
    SHAPE(CPUI_BOOL_OR, 2, true); SHAPE(CPUI_BOOL_XOR, 2, true);
    SHAPE(CPUI_INT_EQUAL, 2, true); SHAPE(CPUI_INT_NOTEQUAL, 2, true);
    SHAPE(CPUI_INT_LESS, 2, true); SHAPE(CPUI_INT_LESSEQUAL, 2, true);
    SHAPE(CPUI_INT_SLESS, 2, true); SHAPE(CPUI_INT_SLESSEQUAL, 2, true);
    SHAPE(CPUI_INT_LEFT, 2, true); SHAPE(CPUI_INT_RIGHT, 2, true);
    SHAPE(CPUI_INT_SRIGHT, 2, true); SHAPE(CPUI_INT_CARRY, 2, true);
    SHAPE(CPUI_INT_SCARRY, 2, true); SHAPE(CPUI_INT_SBORROW, 2, true);
    SHAPE(CPUI_PIECE, 2, true); SHAPE(CPUI_SUBPIECE, 2, true);
    SHAPE(CPUI_POPCOUNT, 1, true); SHAPE(CPUI_LZCOUNT, 1, true);
    SHAPE(CPUI_BRANCH, 1, false); SHAPE(CPUI_CBRANCH, 2, false);
    SHAPE(CPUI_CALL, 1, false); SHAPE(CPUI_BRANCHIND, 1, false);
    SHAPE(CPUI_CALLIND, 1, false); SHAPE(CPUI_RETURN, 1, false);
    SHAPE(CPUI_CALLOTHER, 2, false);
    default: return {nullptr, 0, false};
  }
#undef SHAPE
}
[[noreturn]] void fail(const Instruction& instruction, std::size_t index, const std::string& reason) {
  std::ostringstream out;
  out << "IR guest 0x" << std::hex << instruction.address << (instruction.thumb ? " Thumb" : " ARM")
      << " op " << std::dec << index << ": " << reason;
  throw Error(out.str());
}
bool byte_range(const Varnode& v) {
  return v.size && v.offset <= std::numeric_limits<std::uint64_t>::max() - v.size;
}
bool register_slice(const lift::Program& program, const Varnode& value) {
  for (std::uint64_t byte = value.offset; byte < value.offset + value.size; ++byte) {
    bool covered = false;
    for (const auto& [_, reg] : program.registers) {
      if (reg.space == "register" && byte >= reg.offset && byte - reg.offset < reg.size) { covered = true; break; }
    }
    if (!covered) return false;
  }
  return true;
}
std::int64_t relative_delta(std::uint64_t offset) {
  // SLEIGH PcodeCacher encodes relative op indices as signed uintb values,
  // independent of the varnode's printed width. Match the emitted jump.
  return std::bit_cast<std::int64_t>(offset);
}
std::size_t relative_target(const Instruction& instruction, std::size_t at, const Varnode& target) {
  const auto delta = relative_delta(target.offset);
  if (delta < -static_cast<std::int64_t>(at) || delta > static_cast<std::int64_t>(instruction.ops.size() - at))
    fail(instruction, at, "relative p-code branch outside instruction");
  return static_cast<std::size_t>(static_cast<std::int64_t>(at) + delta);
}
void add_bytes(Bytes& set, const Varnode& varnode) {
  if (varnode.space == "unique") for (unsigned i = 0; i < varnode.size; ++i) set.insert(varnode.offset + i);
}
void verify_instruction(const lift::Program& program, const Instruction& instruction) {
  if ((instruction.thumb ? (instruction.length != 2 && instruction.length != 4) : instruction.length != 4) ||
      instruction.address % (instruction.thumb ? 2 : 4)) fail(instruction, 0, "invalid instruction alignment or length");
  if (std::uint64_t(instruction.address) + instruction.length > (std::uint64_t{1} << 32)) fail(instruction, 0, "instruction address overflows ARM32");
  const auto count = instruction.ops.size();
  if (count > 4096) fail(instruction, 0, "instruction p-code exceeds verifier resource limit");
  std::vector<std::vector<std::size_t>> successors(count + 1), predecessors(count + 1);
  Bytes universe;
  auto edge = [&](std::size_t from, std::size_t to) { successors[from].push_back(to); predecessors[to].push_back(from); };
  for (std::size_t index = 0; index < count; ++index) {
    const auto& op = instruction.ops[index];
    const auto spec = shape(op.opcode);
    if (!spec.name) fail(instruction, index, "unsupported p-code opcode " + std::to_string(op.opcode));
    const auto name = std::string(spec.name);
    if (op.opcode == CPUI_CALLOTHER && op.userop != "setISAMode") fail(instruction, index, "unsupported userop " + op.userop);
    if (op.inputs.size() != spec.inputs) fail(instruction, index, name + " operand count mismatch");
    if (op.output.has_value() != spec.output) fail(instruction, index, name + " output presence mismatch");
    auto check_value = [&](const Varnode& value, bool output) {
      if (!value.size || value.size > 8) fail(instruction, index, name + " unsupported integer width " + std::to_string(value.size));
      if (value.space == "const") { if (output) fail(instruction, index, "constant output is invalid"); return; }
      if (value.space != "unique" && value.space != "register" && value.space != "ram") fail(instruction, index, "unsupported varnode space " + value.space);
      if (!byte_range(value)) fail(instruction, index, "varnode byte range overflows");
      if (value.space == "register" && !register_slice(program, value)) fail(instruction, index, "register slice outside declared register storage");
      if (value.space == "ram" && (value.offset > 0xffffffffULL || value.size > (std::uint64_t{1} << 32) - value.offset)) fail(instruction, index, "RAM varnode exceeds ARM32 address range");
      add_bytes(universe, value);
    };
    if (op.output) check_value(*op.output, true);
    for (std::size_t operand = 0; operand < op.inputs.size(); ++operand) {
      if (!operand && (op.opcode == CPUI_LOAD || op.opcode == CPUI_STORE)) {
        if ((op.inputs[0].space != "spaceid" && op.inputs[0].space != "const") || !op.inputs[0].size || op.inputs[0].size > 8)
          fail(instruction, index, "invalid LOAD/STORE space identifier");
      } else check_value(op.inputs[operand], false);
    }
    auto same_inputs = [&] { if (op.inputs[0].size != op.inputs[1].size) fail(instruction, index, name + " input width mismatch"); };
    auto same_output = [&] { if (op.output->size != op.inputs[0].size) fail(instruction, index, name + " output width mismatch"); };
    switch (op.opcode) {
      case CPUI_COPY: case CPUI_CAST: case CPUI_INT_NEGATE: case CPUI_INT_2COMP: same_output(); break;
      case CPUI_INT_ZEXT: case CPUI_INT_SEXT:
        if (op.output->size < op.inputs[0].size) fail(instruction, index, name + " narrows its input");
        break;
      case CPUI_INT_EQUAL: case CPUI_INT_NOTEQUAL: case CPUI_INT_LESS: case CPUI_INT_LESSEQUAL:
      case CPUI_INT_SLESS: case CPUI_INT_SLESSEQUAL: case CPUI_INT_CARRY: case CPUI_INT_SCARRY: case CPUI_INT_SBORROW:
        same_inputs();
        if (op.output->size != 1) fail(instruction, index, name + " requires one-byte boolean output");
        break;
      case CPUI_BOOL_AND: case CPUI_BOOL_OR: case CPUI_BOOL_XOR:
        if (op.inputs[1].size != 1) fail(instruction, index, name + " requires one-byte booleans");
        [[fallthrough]];
      case CPUI_BOOL_NEGATE:
        if (op.inputs[0].size != 1 || op.output->size != 1) fail(instruction, index, name + " requires one-byte booleans");
        break;
      case CPUI_INT_LEFT: case CPUI_INT_RIGHT: case CPUI_INT_SRIGHT: same_output(); break;
      case CPUI_PIECE:
        if (op.output->size != op.inputs[0].size + op.inputs[1].size) fail(instruction, index, "PIECE output width must equal concatenated inputs");
        break;
      case CPUI_SUBPIECE:
        if (op.inputs[1].space != "const" || op.inputs[1].offset > op.inputs[0].size || op.output->size > op.inputs[0].size - op.inputs[1].offset)
          fail(instruction, index, "SUBPIECE extraction exceeds input");
        break;
      case CPUI_LOAD: case CPUI_STORE:
        if (op.inputs[1].size != 4) fail(instruction, index, "ARM32 memory address must be four bytes");
        break;
      case CPUI_BRANCH: case CPUI_CBRANCH: case CPUI_CALL:
        if (op.inputs[0].space != "ram" && (op.opcode == CPUI_CALL || op.inputs[0].space != "const")) fail(instruction, index, "direct branch target must be RAM or instruction-relative constant");
        if (op.inputs[0].space == "ram" && op.inputs[0].size != 4) fail(instruction, index, "direct ARM32 target must be four bytes");
        if (op.opcode == CPUI_CBRANCH && op.inputs[1].size != 1) fail(instruction, index, "CBRANCH requires one-byte condition");
        break;
      case CPUI_BRANCHIND: case CPUI_CALLIND: case CPUI_RETURN:
        if ((op.inputs[0].space != "register" && op.inputs[0].space != "unique") || op.inputs[0].size != 4)
          fail(instruction, index, "indirect ARM32 target must be a four-byte register or temporary");
        break;
      case CPUI_CALLOTHER: {
        if (op.userop != "setISAMode") fail(instruction, index, "unsupported userop " + op.userop);
        if (op.inputs[0].space != "const" || op.inputs[0].offset >= program.userops.size() || program.userops[op.inputs[0].offset] != op.userop)
          fail(instruction, index, "userop name/index mismatch");
        const auto tb = program.registers.find("TB");
        if (tb == program.registers.end() || op.inputs[1].space != "register" || op.inputs[1].offset != tb->second.offset || op.inputs[1].size != tb->second.size)
          fail(instruction, index, "setISAMode must consume architectural TB register");
        break;
      }
      case CPUI_POPCOUNT: case CPUI_LZCOUNT: break;
      default: same_inputs(); same_output(); break;
    }
    if (op.opcode == CPUI_BRANCH || op.opcode == CPUI_CBRANCH) {
      if (op.inputs[0].space == "const") edge(index, relative_target(instruction, index, op.inputs[0]));
      if (op.opcode == CPUI_CBRANCH) edge(index, index + 1);
    } else if (op.opcode != CPUI_CALL && op.opcode != CPUI_CALLIND && op.opcode != CPUI_BRANCHIND && op.opcode != CPUI_RETURN) edge(index, index + 1);
  }
  std::vector<bool> reachable(count + 1);
  std::deque<std::size_t> pending{0};
  while (!pending.empty()) {
    const auto index = pending.front(); pending.pop_front();
    if (reachable[index]) continue;
    reachable[index] = true;
    for (const auto next : successors[index]) pending.push_back(next);
  }
  // Forward must-analysis: non-entry nodes begin at top (all temporary bytes).
  // Intersect predecessor outputs until no byte can still be removed.
  std::map<std::uint64_t, std::size_t> temporary_indices;
  for (const auto byte : universe) temporary_indices.emplace(byte, temporary_indices.size());
  const auto words = (universe.size() + 63) / 64;
  using Bitset = std::vector<std::uint64_t>;
  const Bitset top(words, ~std::uint64_t{0});
  std::vector<Bitset> incoming(count + 1, top), outgoing(count + 1, top);
  incoming[0].assign(words, 0);
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0; index <= count; ++index) {
      if (!reachable[index]) continue;
      Bitset in = index == 0 ? Bitset(words, 0) : top;
      if (index) for (const auto previous : predecessors[index]) if (reachable[previous]) {
        for (std::size_t word = 0; word < words; ++word) in[word] &= outgoing[previous][word];
      }
      auto out = in;
      if (index < count && instruction.ops[index].output && instruction.ops[index].output->space == "unique") {
        const auto& value = *instruction.ops[index].output;
        for (unsigned byte = 0; byte < value.size; ++byte) {
          const auto bit = temporary_indices.at(value.offset + byte);
          out[bit / 64] |= std::uint64_t{1} << (bit % 64);
        }
      }
      if (in != incoming[index] || out != outgoing[index]) {
        incoming[index] = std::move(in); outgoing[index] = std::move(out); changed = true;
      }
    }
  }
  for (std::size_t index = 0; index < count; ++index) if (reachable[index]) {
    for (const auto& value : instruction.ops[index].inputs) if (value.space == "unique") {
      for (unsigned byte = 0; byte < value.size; ++byte) {
        const auto bit = temporary_indices.at(value.offset + byte);
        if (!(incoming[index][bit / 64] & (std::uint64_t{1} << (bit % 64))))
          fail(instruction, index, "temporary byte read before definite write at unique offset " + std::to_string(value.offset + byte));
      }
    }
  }
}
}  // namespace

void verify(const lift::Program& program) {
  if (program.instructions.empty()) throw Error("IR program contains no instructions");
  for (const auto& [name, reg] : program.registers) {
    if (reg.space != "register" || !byte_range(reg) || reg.offset + reg.size > 1024 * 1024)
      throw Error("IR invalid register metadata: " + name);
  }
  std::set<std::pair<std::uint32_t, bool>> keys;
  for (const auto& instruction : program.instructions) {
    if (!keys.emplace(instruction.address, instruction.thumb).second) fail(instruction, 0, "duplicate address/mode instruction");
    verify_instruction(program, instruction);
  }
}
}  // namespace rearm::ir

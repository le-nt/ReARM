// SPDX-License-Identifier: Apache-2.0
#include "lifter.hpp"

#include <ghidra/loadimage.hh>
#include <ghidra/sleigh.hh>

#include <algorithm>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <tuple>

namespace rearm::lift {
namespace {
using namespace ghidra;

std::string hex(std::uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << value;
  return out.str();
}
std::string escape_xml(const std::string& value) {
  std::string result;
  for (const char c : value) {
    if (c == '&') result += "&amp;";
    else if (c == '<') result += "&lt;";
    else if (c == '>') result += "&gt;";
    else result += c;
  }
  return result;
}
bool executable(const elf::Image& image, std::uint32_t address, std::size_t size) {
  if (std::uint64_t(address) + size > (std::uint64_t{1} << 32)) return false;
  for (std::size_t i = 0; i < size; ++i) {
    bool found = false;
    for (const auto& segment : image.segments()) {
      const auto at = std::uint64_t(address) + i;
      if ((segment.flags & 1) && at >= segment.address && at - segment.address < segment.bytes.size()) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}
class ElfLoadImage final : public LoadImage {
 public:
  explicit ElfLoadImage(const elf::Image& image) : LoadImage("ARM ELF image"), image_(image) {}
  void loadFill(uint1* out, int4 size, const Address& addr) override {
    if (size < 0 || addr.getOffset() > std::numeric_limits<std::uint32_t>::max()) throw elf::Error("Invalid SLEIGH image fetch");
    const auto address = static_cast<std::uint32_t>(addr.getOffset());
    // SLEIGH always requests a 16-byte lookahead window, even for a final
    // two-byte instruction. Padding is parser lookahead only: lift() verifies
    // every byte actually consumed before accepting any instruction.
    for (int4 i = 0; i < size; ++i) {
      const auto at = std::uint64_t(address) + static_cast<std::uint32_t>(i);
      out[i] = at <= std::numeric_limits<std::uint32_t>::max() && image_.mapped(static_cast<std::uint32_t>(at))
                   ? image_.read8(static_cast<std::uint32_t>(at)) : 0;
    }
  }
  std::string getArchType() const override { return "arm"; }
  void adjustVma(long) override { throw elf::Error("SLEIGH must not adjust ELF virtual addresses"); }
 private:
  const elf::Image& image_;
};
Varnode convert(const VarnodeData& data) {
  return {data.space->getName(), data.offset, data.size};
}
class Capture final : public PcodeEmit {
 public:
  explicit Capture(const std::vector<std::string>& userops) : userops_(userops) {}
  void dump(const Address&, OpCode opcode, VarnodeData* output, VarnodeData* inputs, int4 count) override {
    Op op;
    op.opcode = static_cast<int>(opcode);
    if (output) op.output = convert(*output);
    for (int4 i = 0; i < count; ++i) {
      // A p-code LOAD/STORE's space operand encodes a host AddrSpace pointer.
      // Replace it with a deterministic space identity, never serialize pointers.
      if (i == 0 && (opcode == CPUI_LOAD || opcode == CPUI_STORE)) {
        const auto* space = inputs[0].getSpaceFromConst();
        op.inputs.push_back({"spaceid", static_cast<std::uint64_t>(space->getIndex()), inputs[0].size});
        if (space->getName() != "ram") throw elf::Error("Unsupported p-code memory space " + space->getName());
      } else op.inputs.push_back(convert(inputs[i]));
    }
    if (opcode == CPUI_CALLOTHER) {
      if (count < 1 || inputs[0].offset >= userops_.size()) throw elf::Error("Invalid SLEIGH userop index");
      op.userop = userops_[inputs[0].offset];
    }
    ops.push_back(std::move(op));
  }
  std::vector<Op> ops;
 private:
  const std::vector<std::string>& userops_;
};

struct State {
  std::uint32_t address{};
  bool thumb{};
  std::uint32_t condition{};
  std::uint32_t lrset{};
};
using Key = std::pair<std::uint32_t, bool>;

// Determine whether any reachable instruction-local p-code path reaches the end.
// A constant BRANCH target is an op-relative index, not an ARM address.
bool has_fallthrough(const std::vector<Op>& ops) {
  std::vector<bool> visited(ops.size() + 1);
  std::deque<std::size_t> pending{0};
  auto push_relative = [&](std::size_t at, const Varnode& target) {
    std::int64_t delta;
    if (!target.size || target.size > 8) throw elf::Error("Invalid p-code branch width");
    if (target.size == 8) delta = static_cast<std::int64_t>(target.offset);
    else {
      const auto bits = target.size * 8;
      const auto mask = (std::uint64_t{1} << bits) - 1;
      const auto value = target.offset & mask;
      const auto sign = std::uint64_t{1} << (bits - 1);
      delta = value & sign ? static_cast<std::int64_t>(value) - static_cast<std::int64_t>(std::uint64_t{1} << bits) : static_cast<std::int64_t>(value);
    }
    const auto destination = static_cast<std::int64_t>(at) + delta;
    if (destination < 0 || static_cast<std::uint64_t>(destination) > ops.size()) throw elf::Error("Instruction-local p-code branch leaves instruction");
    pending.push_back(static_cast<std::size_t>(destination));
  };
  while (!pending.empty()) {
    const auto at = pending.front(); pending.pop_front();
    if (at == ops.size()) return true;
    if (visited[at]) continue;
    visited[at] = true;
    const auto& op = ops[at];
    if (op.opcode == CPUI_BRANCH || op.opcode == CPUI_CBRANCH) {
      if (op.inputs.empty()) throw elf::Error("P-code branch lacks target");
      if (op.inputs[0].space == "const") push_relative(at, op.inputs[0]);
      if (op.opcode == CPUI_CBRANCH) pending.push_back(at + 1);
    } else if (op.opcode == CPUI_RETURN || op.opcode == CPUI_BRANCHIND) {
      // This path transfers control away from the instruction.
    } else if (op.opcode == CPUI_CALL || op.opcode == CPUI_CALLIND) {
      return true;  // A returning callee needs the guest continuation translated.
    } else pending.push_back(at + 1);
  }
  return false;
}
}  // namespace

Program lift(const elf::Image& image, const std::filesystem::path& sla_path,
             std::uint32_t entry, std::uint32_t maximum,
             const std::vector<std::uint32_t>& extra_entries) {
  if (!maximum) throw elf::Error("Instruction discovery limit must be positive");
  static std::once_flag identifiers;
  std::call_once(identifiers, [] { AttributeId::initialize(); ElementId::initialize(); });
  ElfLoadImage loader(image);
  auto context = std::make_unique<ContextInternal>();
  Sleigh translator(&loader, context.get());
  DocumentStorage storage;
  std::istringstream document("<sleigh>" + escape_xml(sla_path.generic_string()) + "</sleigh>");
  try {
    storage.registerTag(storage.parseDocument(document)->getRoot());
    translator.initialize(storage);
  } catch (const LowlevelError& error) {
    throw elf::Error("Cannot initialize ARM SLEIGH specification: " + error.explain);
  }
  Program result;
  translator.getUserOpNames(result.userops);
  std::map<VarnodeData, std::string> registers;
  translator.getAllRegisters(registers);
  for (const auto& [storage_location, name] : registers) result.registers.emplace(name, convert(storage_location));
  const auto tb = result.registers.at("TB");
  std::deque<State> pending;
  std::map<Key, std::pair<std::uint32_t, std::uint32_t>> discovered;
  auto queue = [&](State state, bool required) {
    const auto width = state.thumb ? 2u : 4u;
    if (state.address % width || !executable(image, state.address, width)) {
      if (required) throw elf::Error("Instruction target is not aligned executable memory at " + hex(state.address));
      return;
    }
    pending.push_back(state);
  };
  queue({entry & ~1u, (entry & 1) != 0, 0, 0}, true);
  for (auto extra : extra_entries) queue({extra & ~1u, (extra & 1) != 0, 0, 0}, true);
  for (const auto& symbol : image.symbols()) {
    if (symbol.defined() && symbol.type == 2) queue({symbol.value & ~1u, (symbol.value & 1) != 0, 0, 0}, false);
  }
  while (!pending.empty()) {
    const auto state = pending.front(); pending.pop_front();
    const Key key{state.address, state.thumb};
    const auto local_context = std::pair{state.condition, state.lrset};
    if (const auto previous = discovered.find(key); previous != discovered.end()) {
      if (previous->second != local_context) throw elf::Error("Conflicting SLEIGH decode contexts at " + hex(state.address));
      continue;
    }
    if (result.instructions.size() >= maximum) throw elf::Error("Instruction discovery exceeds limit " + std::to_string(maximum));
    discovered.emplace(key, local_context);
    const Address address(translator.getDefaultCodeSpace(), state.address);
    Capture capture(result.userops);
    std::uint32_t length;
    try {
      // Upstream caches parsed instructions by address, without ISA/context in
      // that key. Discard those caches before decoding explicitly painted state;
      // the already-loaded SLA base is retained and is not read again.
      auto fresh_context = std::make_unique<ContextInternal>();
      translator.reset(&loader, fresh_context.get());
      translator.initialize(storage);
      context = std::move(fresh_context);
      context->setVariableDefault("TMode", state.thumb ? 1 : 0);
      context->setVariableDefault("LRset", 0);
      context->setVariableDefault("condit", 0);
      context->setVariableRegion("TMode", address, address + 3, state.thumb ? 1 : 0);
      // These are noflow variables. Painting the maximum instruction window
      // leaks a final IT predicate into the following 16-bit instruction.
      // Only SLEIGH's explicit inst_next commits may populate the successor.
      context->setVariableRegion("LRset", address, address + 1, state.lrset);
      context->setVariableRegion("condit", address, address + 1, state.condition);
      const auto decoded = translator.oneInstruction(capture, address);
      if (decoded <= 0 || decoded > 4) throw elf::Error("Invalid decoded ARM instruction length");
      length = static_cast<std::uint32_t>(decoded);
    } catch (const LowlevelError& error) {
      throw elf::Error("SLEIGH decode failed at " + hex(state.address) + (state.thumb ? " (Thumb): " : " (ARM): ") + error.explain);
    }
    if (!executable(image, state.address, length)) throw elf::Error("Decoded instruction crosses executable mapping at " + hex(state.address));
    if (std::uint64_t(state.address) + length > std::numeric_limits<std::uint32_t>::max()) throw elf::Error("Instruction continuation overflows ARM32");
    const auto next = state.address + length;
    const Address next_address(translator.getDefaultCodeSpace(), next);
    const auto& database = static_cast<const ContextDatabase&>(*context);
    const State continuation{next, state.thumb, database.getVariable("condit", next_address), database.getVariable("LRset", next_address)};
    std::optional<bool> target_thumb = state.thumb;
    for (const auto& op : capture.ops) {
      if (op.output && op.output->space == tb.space && op.output->offset == tb.offset && op.output->size == tb.size) {
        if (op.opcode == CPUI_COPY && op.inputs.size() == 1 && op.inputs[0].space == "const") target_thumb = op.inputs[0].offset != 0;
        else target_thumb.reset();
      }
      if ((op.opcode == CPUI_BRANCH || op.opcode == CPUI_CBRANCH || op.opcode == CPUI_CALL) && !op.inputs.empty() && op.inputs[0].space == "ram") {
        if (op.inputs[0].offset > std::numeric_limits<std::uint32_t>::max()) throw elf::Error("Direct p-code target exceeds ARM32");
        const auto target = static_cast<std::uint32_t>(op.inputs[0].offset);
        const auto mode = target & 1 ? true : target_thumb.value_or(state.thumb);
        if ((target & ~1u) == next && mode == state.thumb) queue(continuation, true);
        else queue({target & ~1u, mode, 0, 0}, true);
      }
    }
    if (has_fallthrough(capture.ops)) queue(continuation, true);
    result.instructions.push_back({state.address, state.thumb, length, std::move(capture.ops)});
  }
  std::sort(result.instructions.begin(), result.instructions.end(), [](const auto& a, const auto& b) {
    return std::tie(a.address, a.thumb) < std::tie(b.address, b.thumb);
  });
  return result;
}
}  // namespace rearm::lift

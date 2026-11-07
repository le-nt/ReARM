// SPDX-License-Identifier: Apache-2.0
#include "ir/verify.hpp"
#include <ghidra/opcodes.hh>

#include <functional>
#include <iostream>
#include <limits>

using namespace ghidra;
using rearm::lift::Op;
using rearm::lift::Program;
using rearm::lift::Varnode;
namespace {
Varnode c(std::uint64_t value, unsigned size = 4) { return {"const", value, size}; }
Varnode r(std::uint64_t offset = 0x20, unsigned size = 4) { return {"register", offset, size}; }
Varnode u(std::uint64_t offset = 0x100, unsigned size = 4) { return {"unique", offset, size}; }
Varnode ram(std::uint64_t offset = 0x1000, unsigned size = 4) { return {"ram", offset, size}; }
Op op(int code, std::optional<Varnode> output, std::vector<Varnode> inputs, std::string userop = {}) {
  return {code, std::move(output), std::move(inputs), std::move(userop)};
}
Program program(std::vector<Op> ops = {}) {
  Program result;
  result.registers = {{"r0", r()}, {"r1", r(0x24)}, {"TB", r(0x69, 1)}, {"q0", r(0x300, 16)}};
  result.userops = {"setISAMode"};
  result.instructions.push_back({0x1000, false, 4, std::move(ops)});
  return result;
}
void bad(const Program& p, const std::string& expected) {
  try { rearm::ir::verify(p); } catch (const rearm::ir::Error& error) {
    const auto message = std::string(error.what());
    if (message.find(expected) == std::string::npos) throw std::runtime_error("unexpected diagnostic: " + message);
    return;
  }
  throw std::runtime_error("expected error containing " + expected);
}
}
int main() {
  unsigned tests = 0;
  auto test = [&](const char* name, const std::function<void()>& run) {
    try { run(); ++tests; } catch (const std::exception& error) {
      std::cerr << "FAIL " << name << ": " << error.what() << '\n'; throw;
    }
  };
  try {
    test("nop", [] { rearm::ir::verify(program()); });
    test("copy", [] { rearm::ir::verify(program({op(CPUI_COPY, r(), {c(7)})})); });
    test("empty program", [] { bad({}, "contains no instructions"); });
    test("missing operand", [] { bad(program({op(CPUI_INT_ADD, r(), {c(1)})}), "operand count"); });
    test("missing output", [] { bad(program({op(CPUI_COPY, {}, {c(1)})}), "output presence"); });
    test("unexpected output", [] { bad(program({op(CPUI_BRANCH, r(), {ram()})}), "output presence"); });
    test("unknown opcode", [] { bad(program({op(999, {}, {})}), "unsupported p-code opcode"); });
    test("zero width", [] { bad(program({op(CPUI_COPY, r(0x20, 0), {c(1, 0)})}), "integer width 0"); });
    test("wide integer", [] { bad(program({op(CPUI_COPY, r(0x300, 16), {c(1, 16)})}), "integer width 16"); });
    test("constant output", [] { bad(program({op(CPUI_COPY, c(1), {c(2)})}), "constant output"); });
    test("unsupported space", [] { bad(program({op(CPUI_COPY, r(), {{"other", 0, 4}})}), "unsupported varnode space"); });
    test("register slice bound", [] { bad(program({op(CPUI_COPY, r(0x27, 2), {c(1, 2)})}), "register slice"); });
    test("register aliases", [] { rearm::ir::verify(program({op(CPUI_COPY, r(0x302, 2), {c(1, 2)})})); });
    test("RAM bound", [] { bad(program({op(CPUI_COPY, r(), {ram(0xffffffff)})}), "ARM32 address"); });
    test("unique range overflow", [] { bad(program({op(CPUI_COPY, u(std::numeric_limits<std::uint64_t>::max()), {c(1)})}), "byte range overflows"); });
    test("copy width", [] { bad(program({op(CPUI_COPY, r(), {c(1, 2)})}), "output width"); });
    test("extension narrowing", [] { bad(program({op(CPUI_INT_ZEXT, r(0x20, 2), {c(1)})}), "narrows"); });
    test("binary widths", [] { bad(program({op(CPUI_INT_ADD, r(), {c(1), c(2, 2)})}), "input width"); });
    test("comparison bool result", [] { bad(program({op(CPUI_INT_EQUAL, r(), {c(1), c(2)})}), "one-byte boolean"); });
    test("bool widths", [] { bad(program({op(CPUI_BOOL_AND, r(0x69, 1), {c(1, 1), c(1, 4)})}), "one-byte booleans"); });
    test("memory spaceid", [] { rearm::ir::verify(program({op(CPUI_LOAD, r(), {{"spaceid", 1, 8}, c(0x1000)}), op(CPUI_STORE, {}, {{"spaceid", 1, 8}, c(0x1000), r()})})); });
    test("memory constant spaceid", [] { rearm::ir::verify(program({op(CPUI_LOAD, r(), {c(1, 8), c(0x1000)})})); });
    test("bad memory spaceid", [] { bad(program({op(CPUI_LOAD, r(), {r(), c(0x1000)})}), "space identifier"); });
    test("memory address width", [] { bad(program({op(CPUI_LOAD, r(), {c(1), c(0x1000, 8)})}), "four bytes"); });
    test("valid PIECE", [] { rearm::ir::verify(program({op(CPUI_PIECE, r(), {c(1, 2), c(2, 2)})})); });
    test("bad PIECE", [] { bad(program({op(CPUI_PIECE, r(), {c(1), c(2)})}), "concatenated"); });
    test("valid SUBPIECE", [] { rearm::ir::verify(program({op(CPUI_SUBPIECE, r(0x20, 2), {r(), c(1)})})); });
    test("bad SUBPIECE", [] { bad(program({op(CPUI_SUBPIECE, r(), {r(), c(1)})}), "exceeds input"); });
    test("valid userop", [] { rearm::ir::verify(program({op(CPUI_CALLOTHER, {}, {c(0), r(0x69, 1)}, "setISAMode")})); });
    test("unknown userop", [] { bad(program({op(CPUI_CALLOTHER, {}, {c(0)}, "software_interrupt")}), "unsupported userop software_interrupt"); });
    test("wrong userop index", [] { bad(program({op(CPUI_CALLOTHER, {}, {c(1), r(0x69, 1)}, "setISAMode")}), "name/index"); });
    test("userop requires TB", [] { bad(program({op(CPUI_CALLOTHER, {}, {c(0), c(1, 1)}, "setISAMode")}), "architectural TB"); });
    test("read before write", [] { bad(program({op(CPUI_COPY, r(), {u()})}), "read before definite write"); });
    test("defined temporary", [] { rearm::ir::verify(program({op(CPUI_COPY, u(), {r()}), op(CPUI_COPY, r(), {u()})})); });
    test("partial temporary alias", [] { rearm::ir::verify(program({op(CPUI_COPY, u(), {r()}), op(CPUI_COPY, r(0x20, 2), {u(0x102, 2)})})); });
    test("partial temporary missing bytes", [] { bad(program({op(CPUI_COPY, u(0x100, 1), {c(1, 1)}), op(CPUI_COPY, r(0x20, 2), {u(0x100, 2)})}), "read before definite write"); });
    test("diamond missing definition", [] { bad(program({op(CPUI_CBRANCH, {}, {c(2), r(0x69, 1)}), op(CPUI_COPY, u(), {c(1)}), op(CPUI_COPY, r(), {u()})}), "read before definite write"); });
    test("diamond both defined", [] { rearm::ir::verify(program({op(CPUI_CBRANCH, {}, {c(3), r(0x69, 1)}), op(CPUI_COPY, u(), {c(1)}), op(CPUI_BRANCH, {}, {c(2)}), op(CPUI_COPY, u(), {c(2)}), op(CPUI_COPY, r(), {u()})})); });
    test("loop initialization", [] { rearm::ir::verify(program({op(CPUI_COPY, u(), {c(1)}), op(CPUI_CBRANCH, {}, {c(~std::uint64_t{0}), r(0x69, 1)}), op(CPUI_COPY, r(), {u()})})); });
    test("unreachable read", [] { rearm::ir::verify(program({op(CPUI_BRANCH, {}, {c(2)}), op(CPUI_COPY, r(), {u()})})); });
    test("relative target out of range", [] { bad(program({op(CPUI_BRANCH, {}, {c(2)})}), "outside instruction"); });
    test("negative target out of range", [] { bad(program({op(CPUI_BRANCH, {}, {c(~std::uint64_t{0})})}), "outside instruction"); });
    test("indirect constant rejected", [] { bad(program({op(CPUI_BRANCHIND, {}, {c(0x1000)})}), "four-byte register"); });
    test("instruction alignment", [] { auto p = program(); p.instructions[0].address = 0x1002; bad(p, "alignment or length"); });
    test("duplicate instruction", [] { auto p = program(); p.instructions.push_back(p.instructions[0]); bad(p, "duplicate address/mode"); });
    test("bad register metadata", [] { auto p = program(); p.registers["r0"].offset = 0x100000; bad(p, "invalid register metadata"); });
    test("guest diagnostic", [] { auto p = program({op(999, {}, {})}); p.instructions[0].thumb = true; p.instructions[0].length = 2; bad(p, "IR guest 0x1000 Thumb op 0"); });
  } catch (...) { return 1; }
  std::cout << "PASS " << tests << " normalized IR verifier tests\n";
}

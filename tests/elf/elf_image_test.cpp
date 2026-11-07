// SPDX-License-Identifier: Apache-2.0
#include "elf/elf_image.hpp"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using rearm::elf::Error;
using rearm::elf::Image;
namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t v) {
  b.at(p) = v; b.at(p + 1) = v >> 8;
}
void put32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) b.at(p + i) = static_cast<std::uint8_t>(v >> (8 * i));
}
void require(bool condition, const std::string& why) {
  if (!condition) throw std::runtime_error(why);
}
void fails(const std::function<void()>& fn, const std::string& text) {
  try { fn(); } catch (const Error& error) {
    require(std::string(error.what()).find(text) != std::string::npos, std::string("unexpected diagnostic: ") + error.what());
    return;
  }
  throw std::runtime_error("expected error containing " + text);
}
std::vector<std::uint8_t> fixture() {
  std::vector<std::uint8_t> b(0x500);
  b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F'; b[4] = 1; b[5] = 1; b[6] = 1;
  put16(b, 16, 3); put16(b, 18, 40); put32(b, 20, 1);
  put32(b, 24, 0x1001); put32(b, 28, 52); put32(b, 36, 0x05000000);
  put16(b, 40, 52); put16(b, 42, 32); put16(b, 44, 1);
  put32(b, 52, 1); put32(b, 56, 0x100); put32(b, 60, 0x1000);
  put32(b, 68, 0x400); put32(b, 72, 0x500); put32(b, 76, 7); put32(b, 80, 0x100);
  b[0x100] = 0x12; b[0x101] = 0x34; b[0x102] = 0x56; b[0x103] = 0x78;
  return b;
}
std::vector<std::uint8_t> dynamic_fixture(std::uint32_t reltype = 23, std::uint32_t sym = 0) {
  auto b = fixture();
  put16(b, 44, 2);
  put32(b, 84, 2); put32(b, 88, 0x120); put32(b, 92, 0x1020);
  put32(b, 100, 0x60); put32(b, 104, 0x60); put32(b, 108, 6); put32(b, 112, 4);
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> tags{
      {17, 0x1100}, {18, 8}, {19, 8}, {6, 0x1140}, {11, 16},
      {5, 0x1180}, {10, 32}, {4, 0x11c0}, {0, 0}};
  for (std::size_t i = 0; i < tags.size(); ++i) { put32(b, 0x120 + i * 8, tags[i].first); put32(b, 0x124 + i * 8, tags[i].second); }
  put32(b, 0x200, 0x1200); put32(b, 0x204, reltype | (sym << 8));
  // Symbol zero, defined odd Thumb function, undefined strong, undefined weak.
  put32(b, 0x250, 1); put32(b, 0x254, 0x1301); put32(b, 0x258, 4); b[0x25c] = 0x12; put16(b, 0x25e, 1);
  put32(b, 0x260, 5); b[0x26c] = 0x12;
  put32(b, 0x270, 12); b[0x27c] = 0x22;
  const char names[] = "\0foo\0import\0weak\0";
  for (std::size_t i = 0; i < sizeof(names); ++i) b[0x280 + i] = names[i];
  put32(b, 0x2c0, 1); put32(b, 0x2c4, 4);
  put32(b, 0x300, 0x1234);
  return b;
}
}

int main() {
  std::size_t tests = 0;
  auto test = [&](const char* name, auto fn) {
    try { fn(); ++tests; } catch (const std::exception& error) {
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
      throw;
    }
  };
  try {
    test("segment mapping and BSS", [] {
      const auto image = Image::parse(fixture());
      require(image.entry() == 0x1001, "Thumb entry bit lost");
      require(image.read32(0x1000) == 0x78563412, "vaddr must map file offset");
      require(image.read32(0x1400) == 0, "BSS must be zero");
      require(!image.mapped(0x1500), "must not map bytes past segment");
      fails([&] { image.read32(0x14ff); }, "Unmapped");
      fails([&] { image.read8(0x100); }, "Unmapped");
    });
    test("load bias and sparse image", [] {
      const auto image = Image::parse(fixture(), 0xf0000000);
      require(image.entry() == 0xf0001001, "biased entry");
      require(image.segments()[0].bytes.size() == 0x500, "sparse storage");
      require(image.read16(0xf0001000) == 0x3412, "biased read");
    });
    test("ET_EXEC", [] { auto b = fixture(); put16(b, 16, 2); Image::parse(b); fails([&] { Image::parse(b, 0x1000); }, "ET_EXEC"); });
    test("truncated header", [] { auto b = fixture(); b.resize(20); fails([&] { Image::parse(b); }, "truncated"); });
    test("bad magic", [] { auto b = fixture(); b[0] = 0; fails([&] { Image::parse(b); }, "magic"); });
    test("ELF64", [] { auto b = fixture(); b[4] = 2; fails([&] { Image::parse(b); }, "ELF32"); });
    test("big endian", [] { auto b = fixture(); b[5] = 2; fails([&] { Image::parse(b); }, "little-endian"); });
    test("non ARM", [] { auto b = fixture(); put16(b, 18, 3); fails([&] { Image::parse(b); }, "EM_ARM"); });
    test("unsupported object", [] { auto b = fixture(); put16(b, 16, 1); fails([&] { Image::parse(b); }, "ET_EXEC"); });
    test("file size exceeds mem", [] { auto b = fixture(); put32(b, 72, 3); fails([&] { Image::parse(b); }, "exceeds"); });
    test("truncated segment", [] { auto b = fixture(); b.resize(0x400); fails([&] { Image::parse(b); }, "truncated"); });
    test("invalid alignment", [] { auto b = fixture(); put32(b, 80, 3); fails([&] { Image::parse(b); }, "alignment"); });
    test("bias alignment", [] { auto b = fixture(); fails([&] { Image::parse(b, 1); }, "alignment"); });
    test("address overflow", [] { auto b = fixture(); fails([&] { Image::parse(b, 0xfffff000); }, "overflows"); });
    test("no mapped load", [] { auto b = fixture(); put32(b, 52, 0); fails([&] { Image::parse(b); }, "no nonempty"); });
    test("overlapping segments", [] { auto b = fixture(); put16(b, 44, 2); for (unsigned i = 0; i < 32; ++i) b[84 + i] = b[52 + i]; fails([&] { Image::parse(b); }, "Overlapping"); });
    test("dynamic symbols", [] { auto image = Image::parse(dynamic_fixture(), 0x10000); require(image.symbols().size() == 4, "hash symbol count"); require(image.find_symbol("foo")->value == 0x11301, "symbol bias/tag"); require(!image.find_symbol("import"), "undefined symbol lookup"); });
    test("relative relocation", [] { auto image = Image::parse(dynamic_fixture(), 0x10000); image.relocate(); require(image.read32(0x11200) == 0x11234, "relative result"); fails([&] { image.relocate(); }, "already applied"); });
    test("ABS32", [] { auto image = Image::parse(dynamic_fixture(2, 1), 0x10000); image.relocate(); require(image.read32(0x11200) == 0x12535, "ABS32 result"); });
    test("REL32", [] { auto image = Image::parse(dynamic_fixture(3, 1), 0x10000); image.relocate(); require(image.read32(0x11200) == 0x1335, "REL32 result"); });
    test("GLOB_DAT", [] { auto image = Image::parse(dynamic_fixture(21, 2)); image.relocate([](const auto& symbol) -> std::optional<std::uint32_t> { return symbol.name == "import" ? std::optional<std::uint32_t>(0x7001) : std::nullopt; }); require(image.read32(0x1200) == 0x7001, "GLOB_DAT result"); });
    test("JUMP_SLOT", [] { auto image = Image::parse(dynamic_fixture(22, 1)); image.relocate(); require(image.read32(0x1200) == 0x1301, "JUMP_SLOT result"); });
    test("undefined weak", [] { auto image = Image::parse(dynamic_fixture(21, 3)); image.relocate(); require(image.read32(0x1200) == 0, "weak symbol must resolve zero"); });
    test("undefined strong", [] { auto image = Image::parse(dynamic_fixture(21, 2)); fails([&] { image.relocate(); }, "Unresolved ELF symbol 'import'"); require(image.read32(0x1200) == 0x1234, "failed relocation must not mutate"); });
    test("unsupported relocation", [] { auto image = Image::parse(dynamic_fixture(99)); fails([&] { image.relocate(); }, "Unsupported ARM relocation 99"); });
    test("relative symbol forbidden", [] { auto image = Image::parse(dynamic_fixture(23, 1)); fails([&] { image.relocate(); }, "symbol index zero"); });
    test("bad string range", [] { auto b = dynamic_fixture(); put32(b, 0x250, 32); fails([&] { Image::parse(b); }, "string offset"); });
    test("dynamic must terminate", [] { auto b = dynamic_fixture(); for (unsigned i = 0; i < 12; ++i) { put32(b, 0x120 + i * 8, 1); put32(b, 0x124 + i * 8, 1); } fails([&] { Image::parse(b); }, "DT_NULL"); });
    test("memory wraparound", [] { auto image = Image::parse(fixture()); require(!image.mapped(0xffffffff, 2), "wraparound mapped"); fails([&] { image.read32(0xffffffff); }, "Unmapped"); });
    test("write read", [] { auto image = Image::parse(fixture()); image.write32(0x1101, 0xfedcba98); require(image.read32(0x1101) == 0xfedcba98, "unaligned mapped writes"); });
  } catch (...) { return 1; }
  std::cout << "PASS " << tests << " ELF loader/relocation tests\n";
}

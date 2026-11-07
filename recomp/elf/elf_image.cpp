// SPDX-License-Identifier: Apache-2.0
#include "elf_image.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace rearm::elf {
namespace {
constexpr std::uint64_t address_limit = std::uint64_t{1} << 32;
constexpr std::uint64_t max_image_size = 512ull * 1024 * 1024;
constexpr std::uint32_t max_table_entries = 1'000'000;
constexpr std::uint64_t max_symbol_name_bytes = 64ull * 1024 * 1024;

std::string hex(std::uint32_t n) {
  std::ostringstream out;
  out << "0x" << std::hex << n;
  return out.str();
}
std::uint32_t address(std::uint32_t n, std::uint32_t bias) {
  if (std::uint64_t(n) + bias >= address_limit) throw Error("ELF address plus load bias overflows ARM32");
  return n + bias;
}
struct Reader {
  std::span<const std::uint8_t> bytes;
  void range(std::uint64_t pos, std::uint64_t size) const {
    if (pos > bytes.size() || size > bytes.size() - pos) throw Error("ELF file range is truncated");
  }
  std::uint16_t u16(std::uint64_t pos) const {
    range(pos, 2);
    return bytes[pos] | (std::uint16_t(bytes[pos + 1]) << 8);
  }
  std::uint32_t u32(std::uint64_t pos) const {
    range(pos, 4);
    return bytes[pos] | (std::uint32_t(bytes[pos + 1]) << 8) |
           (std::uint32_t(bytes[pos + 2]) << 16) | (std::uint32_t(bytes[pos + 3]) << 24);
  }
};
}

Image Image::load(const std::filesystem::path& path, std::uint32_t bias) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) throw Error("Cannot open ELF: " + path.string());
  const auto length = in.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > max_image_size) throw Error("ELF file exceeds size limit");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  in.seekg(0);
  if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), length)) throw Error("Cannot read ELF");
  return parse(bytes, bias);
}

Image Image::parse(std::span<const std::uint8_t> bytes, std::uint32_t bias) {
  Reader r{bytes};
  r.range(0, 52);
  if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') throw Error("Invalid ELF magic");
  if (bytes[4] != 1) throw Error("Only ELF32 is supported");
  if (bytes[5] != 1) throw Error("Only little-endian ELF is supported");
  if (bytes[6] != 1 || r.u32(20) != 1) throw Error("Unsupported ELF version");
  if (r.u16(18) != 40) throw Error("ELF machine is not EM_ARM");
  if (r.u16(40) != 52) throw Error("Invalid ELF32 header size");
  Image image;
  std::uint64_t symbol_name_bytes = 0;
  auto account_symbol_name = [&](std::size_t length) {
    symbol_name_bytes += length + 1;
    if (symbol_name_bytes > max_symbol_name_bytes) throw Error("ELF symbol names exceed aggregate resource limit");
  };
  image.type_ = r.u16(16);
  if (image.type_ != 2 && image.type_ != 3) throw Error("Only ET_EXEC and ET_DYN are supported");
  if (image.type_ == 2 && bias) throw Error("ET_EXEC requires zero load bias");
  const auto eabi = r.u32(36) >> 24;
  if (eabi != 0 && eabi != 5) throw Error("Unsupported ARM EABI version");
  image.bias_ = bias;
  image.entry_ = r.u32(24) ? address(r.u32(24), bias) : 0;
  const auto phoff = r.u32(28);
  const auto phnum = r.u16(44);
  if (!phnum || r.u16(42) != 32 || phnum == 0xffff) throw Error("Invalid or unsupported ELF program header table");
  r.range(phoff, std::uint64_t(phnum) * 32);
  std::optional<std::pair<std::uint32_t, std::uint32_t>> dynamic;
  std::uint64_t allocated = 0;
  for (std::uint32_t i = 0; i < phnum; ++i) {
    const std::uint64_t p = std::uint64_t(phoff) + i * 32;
    const auto kind = r.u32(p);
    const auto off = r.u32(p + 4);
    const auto va = r.u32(p + 8);
    const auto filesz = r.u32(p + 16);
    const auto memsz = r.u32(p + 20);
    const auto align = r.u32(p + 28);
    if (kind == 2) {
      if (dynamic) throw Error("Multiple PT_DYNAMIC segments");
      dynamic = {{address(va, bias), filesz}};
    }
    if (kind != 1) continue;
    if (filesz > memsz) throw Error("PT_LOAD file size exceeds memory size");
    r.range(off, filesz);
    if (align > 1 && ((align & (align - 1)) || (va % align != off % align) || (bias % align)))
      throw Error("Invalid PT_LOAD alignment or load bias");
    const auto start = address(va, bias);
    if (std::uint64_t(start) + memsz > address_limit) throw Error("PT_LOAD virtual range overflows ARM32");
    allocated += memsz;
    if (allocated > max_image_size) throw Error("ELF mapped bytes exceed resource limit");
    if (!memsz) continue;
    Segment segment{start, r.u32(p + 24), std::vector<std::uint8_t>(memsz, 0)};
    std::copy_n(bytes.begin() + off, filesz, segment.bytes.begin());
    image.segments_.push_back(std::move(segment));
  }
  std::sort(image.segments_.begin(), image.segments_.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
  if (image.segments_.empty()) throw Error("ELF contains no nonempty PT_LOAD segment");
  for (std::size_t i = 1; i < image.segments_.size(); ++i) {
    const auto& previous = image.segments_[i - 1];
    if (std::uint64_t(previous.address) + previous.bytes.size() > image.segments_[i].address)
      throw Error("Overlapping PT_LOAD ranges are not supported");
  }
  // Section tables enrich analysis only. PT_LOAD/PT_DYNAMIC remain sufficient
  // for loading sectionless Android libraries, and dynamic indices stay first.
  auto append_static_symbols = [&] {
    const auto section_offset = r.u32(32);
    const auto section_count = r.u16(48);
    if (!section_offset && !section_count) return;
    if (!section_offset || !section_count || r.u16(46) != 40) throw Error("Invalid or unsupported section header table");
    r.range(section_offset, std::uint64_t(section_count) * 40);
    for (std::uint32_t section_index = 0; section_index < section_count; ++section_index) {
      const auto section = std::uint64_t(section_offset) + section_index * 40;
      if (r.u32(section + 4) != 2) continue;  // SHT_SYMTAB, not SHT_DYNSYM
      const auto symbols = r.u32(section + 16);
      const auto size = r.u32(section + 20);
      const auto link = r.u32(section + 24);
      if (r.u32(section + 36) != 16 || size % 16 || size / 16 > max_table_entries) throw Error("Invalid static symbol table dimensions");
      if (link >= section_count) throw Error("Static symbol string table link out of bounds");
      r.range(symbols, size);
      const auto strings_section = std::uint64_t(section_offset) + link * 40;
      if (r.u32(strings_section + 4) != 3) throw Error("Static symbol table does not link a string table");
      const auto strings = r.u32(strings_section + 16);
      const auto string_size = r.u32(strings_section + 20);
      r.range(strings, string_size);
      if (image.symbols_.size() + size / 16 > max_table_entries) throw Error("Combined ELF symbols exceed resource limit");
      for (std::uint32_t i = 0; i < size / 16; ++i) {
        const auto pos = std::uint64_t(symbols) + i * 16;
        const auto name_offset = r.u32(pos);
        if (name_offset >= string_size) throw Error("Static symbol string offset out of bounds");
        std::string name;
        auto j = name_offset;
        for (; j < string_size; ++j) {
          const auto c = bytes[std::uint64_t(strings) + j];
          if (!c) break;
          name.push_back(static_cast<char>(c));
          if (name.size() > 65536) throw Error("ELF symbol name exceeds resource limit");
        }
        if (j == string_size) throw Error("Unterminated static symbol name");
        account_symbol_name(name.size());
        const auto info = bytes[pos + 12];
        const auto symbol_section = r.u16(pos + 14);
        if (symbol_section == 0xffff) throw Error("Extended ELF symbol section indices are unsupported");
        if (symbol_section < 0xff00 && symbol_section >= section_count) throw Error("Static symbol section index out of bounds");
        auto value = r.u32(pos + 4);
        if (symbol_section && symbol_section < 0xff00 && (info & 15) != 6) value = address(value, bias);
        image.symbols_.push_back({std::move(name), value, r.u32(pos + 8), static_cast<std::uint8_t>(info >> 4), static_cast<std::uint8_t>(info & 15), symbol_section, static_cast<std::uint8_t>(bytes[pos + 13] & 3), false});
      }
    }
  };
  if (!dynamic) {
    append_static_symbols();
    return image;
  }
  const auto [dynaddr, dynsize] = *dynamic;
  if (dynsize % 8 || dynsize / 8 > max_table_entries || !image.mapped(dynaddr, dynsize)) throw Error("Invalid PT_DYNAMIC range");
  std::map<std::uint32_t, std::uint32_t> tags;
  std::vector<std::uint32_t> needed_offsets;
  bool terminated = false;
  for (std::uint32_t off = 0; off < dynsize; off += 8) {
    const auto tag = image.read32(dynaddr + off);
    if (tag == 0) { terminated = true; break; }
    const auto value = image.read32(dynaddr + off + 4);
    if (tag == 1) {
      if (needed_offsets.size() >= 4096) throw Error("ELF dependency count exceeds resource limit");
      needed_offsets.push_back(value); continue;
    }
    if (tags.contains(tag) && tags[tag] != value) throw Error("Conflicting duplicate dynamic tag");
    tags[tag] = value;
  }
  if (!terminated) throw Error("PT_DYNAMIC lacks DT_NULL terminator");
  auto ptr = [&](std::uint32_t tag) { return address(tags.at(tag), bias); };
  auto dynamic_string = [&](std::uint32_t offset) {
    if (!tags.contains(5) || !tags.contains(10) || offset >= tags[10]) throw Error("Dynamic module string offset out of bounds");
    const auto strings = ptr(5);
    if (!image.mapped(strings, tags[10])) throw Error("Dynamic module string table is unmapped");
    std::string result;
    for (auto i = offset; i < tags[10]; ++i) {
      const auto c = image.read8(strings + i);
      if (!c) return result;
      result.push_back(static_cast<char>(c));
      if (result.size() > 4096) throw Error("Dynamic module string exceeds resource limit");
    }
    throw Error("Unterminated dynamic module string");
  };
  for (const auto offset : needed_offsets) {
    auto name = dynamic_string(offset);
    if (name.empty()) throw Error("Empty DT_NEEDED module name");
    image.dynamic_.needed.push_back(std::move(name));
  }
  if (tags.contains(14)) image.dynamic_.soname = dynamic_string(tags[14]);
  if (tags.contains(12) && tags[12]) image.dynamic_.init = ptr(12);
  if (tags.contains(13) && tags[13]) image.dynamic_.fini = ptr(13);
  auto array_metadata = [&](std::uint32_t pointer_tag, std::uint32_t size_tag,
                            std::uint32_t& array, std::uint32_t& size) {
    if (!tags.contains(pointer_tag) && !tags.contains(size_tag)) return;
    if (!tags.contains(pointer_tag) || !tags.contains(size_tag)) throw Error("Incomplete dynamic initializer array metadata");
    array = ptr(pointer_tag); size = tags[size_tag];
    if (size % 4 || size / 4 > max_table_entries || array % 4 || !image.mapped(array, size)) throw Error("Invalid dynamic initializer array range");
  };
  array_metadata(25, 27, image.dynamic_.init_array, image.dynamic_.init_array_size);
  array_metadata(26, 28, image.dynamic_.fini_array, image.dynamic_.fini_array_size);
  image.dynamic_.symbolic = tags.contains(16) || (tags.contains(30) && (tags[30] & 2));
  image.dynamic_.symbol_versions = tags.contains(0x6ffffff0) || tags.contains(0x6ffffffc) || tags.contains(0x6ffffffe);
  image.dynamic_.preinit_array = tags.contains(32) || tags.contains(33);
  std::map<std::uint32_t, std::size_t> relocation_indices;
  auto read_rels = [&](std::uint32_t table, std::uint32_t size) {
    if (size % 8 || size / 8 > max_table_entries || !image.mapped(table, size)) throw Error("Invalid ELF REL table range");
    for (std::uint32_t off = 0; off < size; off += 8) {
      const auto where = address(image.read32(table + off), bias);
      const auto info = image.read32(table + off + 4);
      const Relocation rel{where, info & 0xff, info >> 8};
      if (const auto existing = relocation_indices.find(where); existing != relocation_indices.end()) {
        const auto& old = image.relocations_[existing->second];
        if (old.type != rel.type || old.symbol != rel.symbol) throw Error("Conflicting relocations at " + hex(where));
      } else {
        relocation_indices.emplace(where, image.relocations_.size());
        image.relocations_.push_back(rel);
      }
    }
  };
  if (tags.contains(7) || tags.contains(8) || tags.contains(9)) throw Error("ELF RELA relocations are not supported");
  if (tags.contains(17) || tags.contains(18)) {
    if (!tags.contains(17) || !tags.contains(18) || (tags.contains(19) && tags[19] != 8)) throw Error("Invalid DT_REL metadata");
    read_rels(ptr(17), tags[18]);
  }
  if (tags.contains(23) || tags.contains(2)) {
    if (!tags.contains(23) || !tags.contains(2) || !tags.contains(20) || tags[20] != 17) throw Error("Only REL PLT relocations are supported");
    read_rels(ptr(23), tags[2]);
  }
  // Packed Android formats must be decoded rather than silently skipped.
  if (tags.contains(0x6000000f) || tags.contains(0x60000011) || tags.contains(36)) throw Error("Packed ELF relocations are not supported");
  std::uint32_t symbol_count = 0;
  for (const auto& rel : image.relocations_) symbol_count = std::max(symbol_count, rel.symbol + 1);
  const auto required_symbols = symbol_count;
  std::optional<std::uint32_t> declared_symbols;
  if (tags.contains(4)) {
    const auto hash = ptr(4);
    const auto buckets = image.read32(hash);
    const auto chains = image.read32(address(hash, 4));
    if (!buckets || buckets > max_table_entries || chains > max_table_entries ||
        !image.mapped(hash, 8ull + (std::uint64_t(buckets) + chains) * 4)) throw Error("Invalid SysV hash dimensions");
    declared_symbols = chains;
    symbol_count = chains;
  }
  if (tags.contains(0x6ffffef5)) {
    const auto hash = ptr(0x6ffffef5);
    const auto nbucket = image.read32(hash);
    const auto first = image.read32(address(hash, 4));
    const auto bloom = image.read32(address(hash, 8));
    if (!bloom || (bloom & (bloom - 1)) || nbucket > max_table_entries || bloom > max_table_entries || first > max_table_entries) throw Error("Invalid GNU hash dimensions");
    const auto buckets = address(hash, 16 + bloom * 4);
    if (!image.mapped(buckets, std::uint64_t(nbucket) * 4)) throw Error("Invalid GNU hash buckets");
    const auto chains = address(buckets, nbucket * 4);
    auto gnu_symbols = first;
    std::uint32_t largest = 0;
    for (std::uint32_t i = 0; i < nbucket; ++i) {
      const auto index = image.read32(buckets + i * 4);
      if (!index) continue;
      if (index < first || index >= max_table_entries) throw Error("GNU hash bucket outside symbol range");
      largest = std::max(largest, index);
    }
    if (largest) {
      auto index = largest;
      for (;;) {
        if (index >= max_table_entries) throw Error("GNU hash chain exceeds resource limit");
        const auto value = image.read32(address(chains, (index - first) * 4));
        gnu_symbols = std::max(gnu_symbols, ++index);
        if (value & 1) break;
      }
    }
    if (declared_symbols && gnu_symbols > *declared_symbols) throw Error("GNU hash symbols exceed SysV symbol count");
    if (!declared_symbols) declared_symbols = gnu_symbols;
    symbol_count = *declared_symbols;
  }
  if (declared_symbols && required_symbols > *declared_symbols) throw Error("Relocation symbol index exceeds declared dynamic symbol count");
  if (symbol_count > max_table_entries) throw Error("ELF symbol table exceeds resource limit");
  if (symbol_count) {
    if (!tags.contains(6) || !tags.contains(5) || !tags.contains(10) || !tags.contains(11) || tags[11] != 16) throw Error("Missing or invalid dynamic symbol metadata");
    const auto symtab = ptr(6);
    const auto strtab = ptr(5);
    const auto strsize = tags[10];
    if (!image.mapped(symtab, std::uint64_t(symbol_count) * 16) || !image.mapped(strtab, strsize)) throw Error("Invalid dynamic symbol/string range");
    for (std::uint32_t i = 0; i < symbol_count; ++i) {
      const auto pos = symtab + i * 16;
      const auto nameoff = image.read32(pos);
      if (nameoff >= strsize) throw Error("Dynamic symbol string offset out of bounds");
      std::string name;
      std::uint32_t j = nameoff;
      for (; j < strsize; ++j) {
        const auto c = image.read8(strtab + j);
        if (!c) break;
        name.push_back(static_cast<char>(c));
        if (name.size() > 65536) throw Error("ELF symbol name exceeds resource limit");
      }
      if (j == strsize) throw Error("Unterminated ELF symbol name");
      account_symbol_name(name.size());
      const auto info = image.read8(pos + 12);
      const auto section = image.read16(pos + 14);
      auto value = image.read32(pos + 4);
      // STT_TLS values are offsets within a TLS image, not guest addresses.
      if (section && section < 0xff00 && (info & 15) != 6) value = address(value, bias);
      image.symbols_.push_back({std::move(name), value, image.read32(pos + 8), static_cast<std::uint8_t>(info >> 4), static_cast<std::uint8_t>(info & 15), section, static_cast<std::uint8_t>(image.read8(pos + 13) & 3), true});
    }
  }
  append_static_symbols();
  return image;
}

const Segment& Image::segment_at(std::uint32_t addr) const {
  auto it = std::upper_bound(segments_.begin(), segments_.end(), addr, [](auto a, const auto& s) { return a < s.address; });
  if (it != segments_.begin()) {
    --it;
    if (std::uint64_t(addr) - it->address < it->bytes.size()) return *it;
  }
  throw Error("Unmapped guest address " + hex(addr));
}
Segment& Image::segment_at(std::uint32_t addr) {
  return const_cast<Segment&>(std::as_const(*this).segment_at(addr));
}
bool Image::mapped(std::uint32_t addr, std::size_t size) const {
  if (size > address_limit - addr) return false;
  std::uint64_t current = addr;
  const auto end = current + size;
  try {
    while (current < end) {
      const auto& segment = segment_at(static_cast<std::uint32_t>(current));
      current = std::min(end, std::uint64_t(segment.address) + segment.bytes.size());
    }
    return true;
  } catch (const Error&) { return false; }
}
std::uint8_t Image::read8(std::uint32_t addr) const {
  const auto& segment = segment_at(addr);
  return segment.bytes[addr - segment.address];
}
std::vector<std::uint8_t> Image::read(std::uint32_t addr, std::size_t size) const {
  if (!mapped(addr, size)) throw Error("Unmapped guest read at " + hex(addr));
  std::vector<std::uint8_t> bytes(size);
  for (std::size_t i = 0; i < size; ++i) bytes[i] = read8(addr + static_cast<std::uint32_t>(i));
  return bytes;
}
std::uint16_t Image::read16(std::uint32_t addr) const {
  if (!mapped(addr, 2)) throw Error("Unmapped guest 16-bit read at " + hex(addr));
  return read8(addr) | (std::uint16_t(read8(addr + 1)) << 8);
}
std::uint32_t Image::read32(std::uint32_t addr) const {
  if (!mapped(addr, 4)) throw Error("Unmapped guest 32-bit read at " + hex(addr));
  return read8(addr) | (std::uint32_t(read8(addr + 1)) << 8) | (std::uint32_t(read8(addr + 2)) << 16) | (std::uint32_t(read8(addr + 3)) << 24);
}
void Image::write32(std::uint32_t addr, std::uint32_t value) {
  if (!mapped(addr, 4)) throw Error("Unmapped guest 32-bit write at " + hex(addr));
  for (unsigned i = 0; i < 4; ++i) {
    auto& segment = segment_at(addr + i);
    segment.bytes[addr + i - segment.address] = static_cast<std::uint8_t>(value >> (8 * i));
  }
}
std::optional<Symbol> Image::find_symbol(std::string_view name) const {
  for (const auto& symbol : symbols_) if (symbol.name == name && symbol.defined()) return symbol;
  return {};
}
void Image::relocate(const Resolver& resolver, bool interpose_defined) {
  if (relocated_) throw Error("ELF REL relocations already applied");
  std::vector<std::pair<std::uint32_t, std::uint32_t>> writes;
  for (const auto& rel : relocations_) {
    if (!rel.type) continue;
    if (rel.type != 2 && rel.type != 3 && rel.type != 21 && rel.type != 22 && rel.type != 23)
      throw Error("Unsupported ARM relocation " + std::to_string(rel.type) + " at " + hex(rel.address));
    if (rel.address & 3) throw Error("ARM dynamic relocation target is not word-aligned at " + hex(rel.address));
    const auto addend = read32(rel.address);
    std::uint32_t value = 0;
    if (rel.type == 23) {
      if (rel.symbol) throw Error("R_ARM_RELATIVE must have symbol index zero");
      value = bias_ + addend;
    } else {
      if (rel.symbol >= symbols_.size()) throw Error("Relocation symbol index out of bounds");
      const auto& sym = symbols_[rel.symbol];
      std::optional<std::uint32_t> target;
      const bool preemptible = sym.defined() && interpose_defined && resolver &&
          (sym.binding == 1 || sym.binding == 2) && sym.visibility == 0 && !dynamic_.symbolic;
      if (preemptible || !sym.defined()) { if (resolver) target = resolver(sym); }
      if (!target && sym.defined()) target = sym.value;
      if (!target && (sym.binding == 2 || rel.symbol == 0)) target = 0;
      if (!target) throw Error("Unresolved ELF symbol '" + sym.name + "' at " + hex(rel.address));
      const auto thumb = sym.type == 2 ? *target & 1u : 0u;
      const auto symbol_address = *target & ~thumb;
      if (rel.type == 2) value = (symbol_address + addend) | thumb;
      else if (rel.type == 3) value = ((symbol_address + addend) | thumb) - rel.address;
      else value = *target;
    }
    writes.emplace_back(rel.address, value);
  }
  for (const auto& [addr, value] : writes) write32(addr, value);
  relocated_ = true;
}

Image Image::combine(std::vector<Image> images) {
  if (images.empty()) throw Error("Cannot combine an empty ELF module list");
  Image combined;
  combined.bias_ = images.front().bias_;
  combined.entry_ = images.front().entry_;
  combined.type_ = images.front().type_;
  std::uint64_t bytes = 0;
  for (auto& image : images) {
    if (!image.relocated_) throw Error("Only already-relocated ELF images may be combined");
    for (auto& segment : image.segments_) {
      bytes += segment.bytes.size();
      if (bytes > max_image_size) throw Error("Combined ELF images exceed mapped-byte limit");
      combined.segments_.push_back(std::move(segment));
    }
    if (combined.symbols_.size() + image.symbols_.size() > max_table_entries) throw Error("Combined ELF symbols exceed resource limit");
    for (auto& symbol : image.symbols_) combined.symbols_.push_back(std::move(symbol));
  }
  std::sort(combined.segments_.begin(), combined.segments_.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
  for (std::size_t i = 1; i < combined.segments_.size(); ++i) {
    const auto& previous = combined.segments_[i - 1];
    if (std::uint64_t(previous.address) + previous.bytes.size() > combined.segments_[i].address) throw Error("Overlapping ELF module mappings");
  }
  combined.relocated_ = true;
  return combined;
}
}  // namespace rearm::elf

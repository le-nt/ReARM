// sla_probe: M0 smoke test for the SLEIGH integration.
// Decodes a range of an ARM32 ELF image with ARM:LE:32:v7, in ARM or Thumb mode, and prints
// disassembly plus raw p-code. Used to confirm that libsla links and that TMode context and
// IT-block context propagate across consecutive instructions.
//
//   sla_probe <ARM7_le.sla> <file.so> <vaddr> <count> [arm|thumb]
//
// The image is read with vaddr == file offset, which holds for the R+X segment of the
// Android .so files we target; the real ELF loader replaces this in M1.

#include <ghidra/loadimage.hh>
#include <ghidra/sleigh.hh>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

using namespace ghidra;

namespace {

class FileImage : public LoadImage {
 public:
  explicit FileImage(std::vector<uint1> bytes) : LoadImage("probe"), bytes_(std::move(bytes)) {}

  void loadFill(uint1 *ptr, int4 size, const Address &addr) override {
    uintb start = addr.getOffset();
    for (int4 i = 0; i < size; ++i) {
      uintb at = start + i;
      ptr[i] = at < bytes_.size() ? bytes_[at] : 0;
    }
  }
  std::string getArchType() const override { return "arm"; }
  void adjustVma(long) override {}

 private:
  std::vector<uint1> bytes_;
};

class AsmOut : public AssemblyEmit {
 public:
  void dump(const Address &addr, const std::string &mnem, const std::string &body) override {
    std::printf("%08llx  %-10s %s\n", static_cast<unsigned long long>(addr.getOffset()), mnem.c_str(),
                body.c_str());
  }
};

void printVarnode(const VarnodeData &v) {
  std::printf("(%s,0x%llx,%u)", v.space->getName().c_str(), static_cast<unsigned long long>(v.offset), v.size);
}

class PcodeOut : public PcodeEmit {
 public:
  void dump(const Address &, OpCode opc, VarnodeData *out, VarnodeData *vars, int4 isize) override {
    std::printf("            ");
    if (out != nullptr) {
      printVarnode(*out);
      std::printf(" = ");
    }
    std::printf("%s", get_opname(opc));
    for (int4 i = 0; i < isize; ++i) {
      std::printf(" ");
      printVarnode(vars[i]);
    }
    std::printf("\n");
  }
};

}  // namespace

int main(int argc, char **argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: %s <ARM7_le.sla> <file.so> <vaddr> <count> [arm|thumb]\n", argv[0]);
    return 2;
  }
  std::ifstream in(argv[2], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", argv[2]);
    return 1;
  }
  std::vector<uint1> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  uintb vaddr = std::strtoull(argv[3], nullptr, 0) & ~uintb(1);
  int count = std::atoi(argv[4]);
  bool thumb = argc > 5 ? std::string(argv[5]) == "thumb" : (std::strtoull(argv[3], nullptr, 0) & 1) != 0;

  AttributeId::initialize();
  ElementId::initialize();

  FileImage image(std::move(bytes));
  ContextInternal context;
  Sleigh trans(&image, &context);
  DocumentStorage storage;
  std::istringstream spec("<sleigh>" + std::string(argv[1]) + "</sleigh>");
  storage.registerTag(storage.parseDocument(spec)->getRoot());
  trans.initialize(storage);

  // Mirrors ARMtTHUMB.pspec: TMode selects the ISA; LRset tracks "mov lr,pc; bx" call idioms.
  context.setVariableDefault("TMode", thumb ? 1 : 0);
  context.setVariableDefault("LRset", 0);

  AsmOut asmout;
  PcodeOut pcode;
  Address addr(trans.getDefaultCodeSpace(), vaddr);
  for (int i = 0; i < count; ++i) {
    // TMode is a flowing context variable: a BLX commit paints it from the branch target
    // onwards, which would flip the rest of this function's ISA. The driver owns the mode, so
    // repaint it over this instruction's bytes (max 4) before every decode. IT-block state
    // (condit) and LRset are noflow commits to inst_next and are left to SLEIGH.
    context.setVariableRegion("TMode", addr, addr + 3, thumb ? 1 : 0);
    try {
      trans.printAssembly(asmout, addr);
      int4 len = trans.oneInstruction(pcode, addr);
      addr = addr + len;
    } catch (const LowlevelError &err) {
      std::printf("%08llx  <decode error: %s>\n", static_cast<unsigned long long>(addr.getOffset()),
                  err.explain.c_str());
      return 1;
    }
  }
  return 0;
}

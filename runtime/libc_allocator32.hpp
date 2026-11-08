// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "heap32.hpp"
#include "libc_imports32.hpp"
namespace rearm::libc32 {
inline constexpr std::array<Import,5> allocator_imports{{
 {"malloc",import_base+0x100,1},{"calloc",import_base+0x104,2},
 {"realloc",import_base+0x108,2},{"free",import_base+0x10c,1},{"__errno",import_base+0x110,0}
}};
inline std::optional<Import> allocatorImport(std::string_view name){for(auto b:allocator_imports)if(b.name==name)return b;return {};}
class AllocatorBridge {
 public:
  AllocatorBridge(jni32::GuestMemory&,Heap&);
  bool dispatch(std::array<std::uint32_t,16>& regs,bool& thumb,jni32::ThreadId);
  std::uint32_t errnoPointer(jni32::ThreadId);
 private:
  jni32::GuestMemory& memory_;Heap& heap_;
  std::map<jni32::ThreadId,std::uint32_t> errno_;
  std::mutex mutex_;
  std::uint32_t errnoLocked(jni32::ThreadId);
};
}

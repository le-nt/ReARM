// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "libc_memory32.hpp"
#include <array>
#include <optional>
#include <string_view>
namespace rearm::libc32 {
struct Import {std::string_view name;std::uint32_t address,words;};
inline constexpr std::uint32_t import_base=0xffd00000u;
inline constexpr std::array<Import,10> memory_imports{{
 {"memcpy",import_base,3},{"memmove",import_base+4,3},{"memset",import_base+8,3},
 {"memcmp",import_base+12,3},{"strlen",import_base+16,1},{"strnlen",import_base+20,2},
 {"strcmp",import_base+24,2},{"strncmp",import_base+28,3},
 {"strcpy",import_base+32,2},{"strncpy",import_base+36,3}
}};
inline std::optional<Import> memoryImport(std::string_view name){for(auto b:memory_imports)if(b.name==name)return b;return {};}
inline std::optional<Import> memoryImport(std::uint32_t address){for(auto b:memory_imports)if(b.address==address)return b;return {};}
// False leaves state unchanged for nonbindings. Current imports use only r0-r2;
// all other architectural registers/flags are preserved by this host contract.
bool dispatchMemory(std::array<std::uint32_t,16>& regs,bool& thumb,jni32::GuestMemory&);
}

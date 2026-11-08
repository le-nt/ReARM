// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "jni32.hpp"
namespace rearm::libc32 {
// Guest size_t/address words are 32 bits. Bounds are explicit runtime limits,
// not successful truncation. Import resolution/errno/heap services are separate.
// Zero-count routines perform no access (including null addresses). Overlapping
// memcpy/strcpy/strncpy inputs are diagnosed; copy(...,true) implements memmove.
struct MemoryLimits {std::uint32_t transfer_bytes=16*1024*1024;std::uint32_t string_bytes=1024*1024;};
class MemoryServices {
 public:
  explicit MemoryServices(jni32::GuestMemory&,MemoryLimits limits={});
  std::uint32_t copy(std::uint32_t dst,std::uint32_t src,std::uint32_t count,bool allow_overlap=false);
  std::uint32_t fill(std::uint32_t dst,std::uint32_t value,std::uint32_t count);
  std::int32_t compare(std::uint32_t a,std::uint32_t b,std::uint32_t count);
  std::uint32_t length(std::uint32_t text);
  std::uint32_t boundedLength(std::uint32_t text,std::uint32_t limit);
  std::int32_t compareStrings(std::uint32_t a,std::uint32_t b);
  std::int32_t compareStrings(std::uint32_t a,std::uint32_t b,std::uint32_t limit);
  std::uint32_t copyString(std::uint32_t dst,std::uint32_t src);
  std::uint32_t copyString(std::uint32_t dst,std::uint32_t src,std::uint32_t count);
 private:
  jni32::GuestMemory& memory_;
  MemoryLimits limits_;
  void transferRange(std::uint32_t,std::uint32_t)const;
  std::uint8_t character(std::uint32_t,std::uint32_t)const;
};
}

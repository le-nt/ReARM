// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "jni32.hpp"
#include <map>
#include <mutex>
namespace rearm::libc32 {
struct HeapStats {std::uint32_t live_allocations{},free_bytes{},largest_free{};std::uint64_t reserved_bytes{},peak_reserved_bytes{};};
// A bounded allocator over a caller-supplied mapped RW guest arena, max128MiB.
// Addresses are 8-byte aligned, never host pointers. OOM/size overflow returns0.
// malloc(0)/calloc(0,...) allocate one minimum block; realloc(p,0) frees p.
// Invalid/interior/double frees are diagnosed rather than silently accepted.
// Heap operations serialize internally; callers must synchronize unrelated
// accesses through GuestMemory. errno/TLS and ELF import binding are separate.
class Heap {
 public:
  Heap(jni32::GuestMemory&,std::uint32_t base,std::uint32_t bytes,std::uint32_t max_allocations=65536);
  std::uint32_t allocate(std::uint32_t size);
  std::uint32_t allocateZeroed(std::uint32_t count,std::uint32_t size);
  std::uint32_t reallocate(std::uint32_t address,std::uint32_t size);
  void release(std::uint32_t address);
  HeapStats stats()const;
 private:
  struct Block {std::uint32_t requested,reserved;};
  jni32::GuestMemory& memory_;
  std::map<std::uint32_t,std::uint32_t> free_;
  std::map<std::uint32_t,Block> live_;
  std::uint64_t reserved_=0,peak_=0;
  std::uint32_t max_allocations_;
  mutable std::mutex mutex_;
  std::uint32_t allocateLocked(std::uint32_t);
  void releaseLocked(std::uint32_t);
  void addFree(std::uint32_t,std::uint32_t);
};
}

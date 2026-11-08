// SPDX-License-Identifier: Apache-2.0
#include "libc_allocator32.hpp"
#include "aapcs32.hpp"
namespace rearm::libc32 {
AllocatorBridge::AllocatorBridge(jni32::GuestMemory& memory,Heap& heap):memory_(memory),heap_(heap){}
std::uint32_t AllocatorBridge::errnoLocked(jni32::ThreadId thread){
 if(auto found=errno_.find(thread);found!=errno_.end())return found->second;
 if(errno_.size()>=4096)throw jni32::MemoryError("Guest errno thread limit exceeded");
 const auto at=memory_.allocate(4,4);if(!at||(at&3)||std::uint64_t(at)+4>0x100000000ULL)throw jni32::MemoryError("Invalid guest errno storage");
 if(heap_.overlapsArena(at,4))throw jni32::MemoryError("Guest errno allocation overlaps heap arena");
 for(auto [_,used]:errno_)if(used==at)throw jni32::MemoryError("Guest errno allocator reused another thread's storage");
 memory_.writeU32(at,0);errno_.emplace(thread,at);return at;
}
std::uint32_t AllocatorBridge::errnoPointer(jni32::ThreadId thread){std::lock_guard lock(mutex_);return errnoLocked(thread);}
bool AllocatorBridge::dispatch(std::array<std::uint32_t,16>& r,bool& thumb,jni32::ThreadId thread){
 std::optional<Import> binding;for(auto candidate:allocator_imports)if(candidate.address==(r[15]&~1u))binding=candidate;
 if(!binding)return false;
 if(thumb||(r[15]&3))throw jni32::Error("Allocator import requires aligned ARM callback target");
 const auto lr=r[14];if(!(lr&1)&&(lr&3))throw jni32::Error("Allocator callback return alignment");
 auto args=aapcs32::marshalWords(r,memory_,binding->words);
 std::lock_guard lock(mutex_);const auto cell=errnoLocked(thread);std::uint32_t result=0;bool failed=false;
 if(binding->name=="malloc"){result=heap_.allocate(args[0]);failed=!result;}
 else if(binding->name=="calloc"){result=heap_.allocateZeroed(args[0],args[1]);failed=!result;}
 else if(binding->name=="realloc"){result=heap_.reallocate(args[0],args[1]);failed=!result&&(args[1]!=0||args[0]==0);}
 else if(binding->name=="free")heap_.release(args[0]);
 else if(binding->name=="__errno")result=cell;
 else throw jni32::UnsupportedFunction("Unknown allocator import");
 // Linux/Android ARM ENOMEM. Successful calls and free preserve errno.
 if(failed)memory_.writeU32(cell,12);
 r[0]=result;r[15]=lr&~1u;thumb=(lr&1)!=0;return true;
}
}

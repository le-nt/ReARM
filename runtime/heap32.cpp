// SPDX-License-Identifier: Apache-2.0
#include "heap32.hpp"
#include <algorithm>
namespace rearm::libc32 {
namespace {
std::uint64_t rounded(std::uint32_t size){return (std::uint64_t(std::max(1u,size))+7)&~std::uint64_t(7);}
}
Heap::Heap(jni32::GuestMemory& memory,std::uint32_t base,std::uint32_t bytes,std::uint32_t max_allocations):memory_(memory),max_allocations_(max_allocations){
 if(!base||(base&7)||(bytes&7)||bytes<8||bytes>128*1024*1024||std::uint64_t(base)+bytes>0x100000000ULL)
  throw jni32::MemoryError("Invalid bounded guest heap arena");
 if(!max_allocations||max_allocations>1048576)throw jni32::MemoryError("Invalid guest heap metadata limit");
 free_.emplace(base,bytes);
}
std::uint32_t Heap::allocateLocked(std::uint32_t size){
 if(live_.size()>=max_allocations_)return 0;
 const auto count=rounded(size);
 for(auto it=free_.begin();it!=free_.end();++it)if(it->second>=count){
  const auto at=it->first,available=it->second;
  live_.emplace(at,Block{size,std::uint32_t(count)});
  free_.erase(it);if(available>count)free_.emplace(at+std::uint32_t(count),available-std::uint32_t(count));
  reserved_+=count;peak_=std::max(peak_,reserved_);return at;
 }
 return 0;
}
void Heap::addFree(std::uint32_t at,std::uint32_t size){
 auto next=free_.lower_bound(at);
 if(next!=free_.begin()){
  auto previous=std::prev(next);
  if(std::uint64_t(previous->first)+previous->second==at){at=previous->first;size+=previous->second;free_.erase(previous);}
 }
 if(next!=free_.end()&&std::uint64_t(at)+size==next->first){size+=next->second;free_.erase(next);}
 free_.emplace(at,size);
}
void Heap::releaseLocked(std::uint32_t at){
 if(!at)return;
 auto found=live_.find(at);if(found==live_.end())throw jni32::MemoryError("Invalid/interior/double free in guest heap");
 const auto bytes=found->second.reserved;live_.erase(found);reserved_-=bytes;addFree(at,bytes);
}
std::uint32_t Heap::allocate(std::uint32_t size){std::lock_guard lock(mutex_);return allocateLocked(size);}
void Heap::release(std::uint32_t at){std::lock_guard lock(mutex_);releaseLocked(at);}
std::uint32_t Heap::allocateZeroed(std::uint32_t count,std::uint32_t size){
 const auto product=std::uint64_t(count)*size;if(product>0xffffffffULL)return 0;
 std::lock_guard lock(mutex_);auto at=allocateLocked(std::uint32_t(product));if(!at)return 0;
 try{memory_.write(at,std::vector<std::uint8_t>(std::max(1u,std::uint32_t(product))));}
 catch(...){releaseLocked(at);throw;}return at;
}
std::uint32_t Heap::reallocate(std::uint32_t at,std::uint32_t size){
 std::lock_guard lock(mutex_);if(!at)return allocateLocked(size);
 auto found=live_.find(at);if(found==live_.end())throw jni32::MemoryError("Invalid guest realloc pointer");
 if(!size){releaseLocked(at);return 0;}
 auto& block=found->second;const auto count=rounded(size);const auto old=block;
 if(count<=block.reserved){
  block={size,std::uint32_t(count)};
  if(old.reserved>count){reserved_-=old.reserved-count;addFree(at+std::uint32_t(count),old.reserved-std::uint32_t(count));}
  return at;
 }
 const auto end=std::uint64_t(at)+block.reserved;
 auto next=end<0x100000000ULL?free_.find(std::uint32_t(end)):free_.end();
 const auto extra=count-block.reserved;
 if(next!=free_.end()&&next->second>=extra){
  const auto available=next->second;free_.erase(next);
  if(available>extra)free_.emplace(std::uint32_t(end+extra),available-std::uint32_t(extra));
  block={size,std::uint32_t(count)};reserved_+=extra;peak_=std::max(peak_,reserved_);return at;
 }
 auto fresh=allocateLocked(size);if(!fresh)return 0;
 try{std::vector<std::uint8_t> contents(std::min(old.requested,size));memory_.read(at,contents);memory_.write(fresh,contents);}
 catch(...){releaseLocked(fresh);throw;}
 releaseLocked(at);return fresh;
}
HeapStats Heap::stats()const{
 std::lock_guard lock(mutex_);HeapStats result;result.live_allocations=std::uint32_t(live_.size());result.reserved_bytes=reserved_;result.peak_reserved_bytes=peak_;
 for(const auto& [_,bytes]:free_){result.free_bytes+=bytes;result.largest_free=std::max(result.largest_free,bytes);}return result;
}
}

// SPDX-License-Identifier: Apache-2.0
#include "libc_memory32.hpp"
#include <array>
namespace rearm::libc32 {
namespace {
bool overlap(std::uint32_t a,std::uint32_t as,std::uint32_t b,std::uint32_t bs){
 return as&&bs&&std::uint64_t(a)<std::uint64_t(b)+bs&&std::uint64_t(b)<std::uint64_t(a)+as;
}
}
MemoryServices::MemoryServices(jni32::GuestMemory& memory,MemoryLimits limits):memory_(memory),limits_(limits){
 if(!limits.transfer_bytes||!limits.string_bytes||limits.string_bytes>limits.transfer_bytes)
  throw jni32::Error("Invalid libc32 memory limits");
}
void MemoryServices::transferRange(std::uint32_t at,std::uint32_t count)const{
 if(count>limits_.transfer_bytes)throw jni32::MemoryError("libc32 transfer exceeds configured byte limit");
 if(count&&(!at||std::uint64_t(at)+count>0x100000000ULL))throw jni32::MemoryError("libc32 null or wrapping guest range");
}
std::uint8_t MemoryServices::character(std::uint32_t at,std::uint32_t index)const{
 if(!at||std::uint64_t(at)+index>=0x100000000ULL)throw jni32::MemoryError("libc32 null or wrapping string address");
 std::array<std::uint8_t,1> out{};memory_.read(at+index,out);return out[0];
}
std::uint32_t MemoryServices::copy(std::uint32_t dst,std::uint32_t src,std::uint32_t count,bool allow_overlap){
 transferRange(dst,count);transferRange(src,count);if(!count)return dst;
 if(!allow_overlap&&overlap(dst,count,src,count))throw jni32::MemoryError("libc32 memcpy overlapping ranges (use memmove)");
 std::vector<std::uint8_t> bytes(count);memory_.read(src,bytes);memory_.write(dst,bytes);return dst;
}
std::uint32_t MemoryServices::fill(std::uint32_t dst,std::uint32_t value,std::uint32_t count){
 transferRange(dst,count);if(count)memory_.write(dst,std::vector<std::uint8_t>(count,std::uint8_t(value)));return dst;
}
std::int32_t MemoryServices::compare(std::uint32_t a,std::uint32_t b,std::uint32_t count){
 transferRange(a,count);transferRange(b,count);if(!count)return 0;
 std::vector<std::uint8_t> x(count),y(count);memory_.read(a,x);memory_.read(b,y);
 for(std::uint32_t n=0;n<count;++n)if(x[n]!=y[n])return std::int32_t(x[n])-std::int32_t(y[n]);return 0;
}
std::uint32_t MemoryServices::length(std::uint32_t text){
 for(std::uint32_t n=0;n<limits_.string_bytes;++n)if(!character(text,n))return n;
 throw jni32::MemoryError("libc32 unterminated string exceeds configured byte limit");
}
std::uint32_t MemoryServices::boundedLength(std::uint32_t text,std::uint32_t limit){
 if(limit>limits_.string_bytes)throw jni32::MemoryError("libc32 string scan exceeds configured byte limit");
 for(std::uint32_t n=0;n<limit;++n)if(!character(text,n))return n;return limit;
}
std::int32_t MemoryServices::compareStrings(std::uint32_t a,std::uint32_t b){
 for(std::uint32_t n=0;n<limits_.string_bytes;++n){auto x=character(a,n),y=character(b,n);if(x!=y)return std::int32_t(x)-std::int32_t(y);if(!x)return 0;}
 throw jni32::MemoryError("libc32 string comparison exceeds configured byte limit");
}
std::int32_t MemoryServices::compareStrings(std::uint32_t a,std::uint32_t b,std::uint32_t limit){
 if(limit>limits_.string_bytes)throw jni32::MemoryError("libc32 string comparison exceeds configured byte limit");
 for(std::uint32_t n=0;n<limit;++n){auto x=character(a,n),y=character(b,n);if(x!=y)return std::int32_t(x)-std::int32_t(y);if(!x)return 0;}return 0;
}
std::uint32_t MemoryServices::copyString(std::uint32_t dst,std::uint32_t src){return copy(dst,src,length(src)+1);}
std::uint32_t MemoryServices::copyString(std::uint32_t dst,std::uint32_t src,std::uint32_t count){
 transferRange(dst,count);if(!count)return dst;
 // strncpy reads only through the first terminator, then pads the destination.
 std::vector<std::uint8_t> bytes(count);std::uint32_t scanned=0;
 for(;scanned<count;++scanned){if(scanned>=limits_.string_bytes)throw jni32::MemoryError("libc32 strncpy scan exceeds configured byte limit");bytes[scanned]=character(src,scanned);if(!bytes[scanned]){++scanned;break;}}
 if(overlap(dst,count,src,scanned))throw jni32::MemoryError("libc32 strncpy overlapping ranges");
 memory_.write(dst,bytes);return dst;
}
}

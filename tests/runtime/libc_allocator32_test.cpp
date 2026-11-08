// SPDX-License-Identifier: Apache-2.0
#include "runtime/libc_allocator32.hpp"
#include <algorithm>
#include <iostream>
using namespace rearm;
struct Memory:jni32::GuestMemory{
 std::array<std::uint8_t,65536> data{};std::uint32_t next=4096,forced=0;
 std::uint32_t allocate(std::uint32_t size,std::uint32_t align)override{if(forced)return forced;auto at=(next+align-1)&~(align-1);next=at+size;return at;}
 void read(std::uint32_t at,std::span<std::uint8_t> out)const override{if(!at||std::uint64_t(at)+out.size()>data.size())throw jni32::MemoryError("read");std::copy_n(data.begin()+at,out.size(),out.begin());}
 void write(std::uint32_t at,std::span<const std::uint8_t> in)override{if(!at||std::uint64_t(at)+in.size()>data.size())throw jni32::MemoryError("write");std::copy(in.begin(),in.end(),data.begin()+at);}
};
void check(bool b){if(!b)throw std::runtime_error("check failed");}
template<class F>void reject(F f){try{f();}catch(const jni32::Error&){return;}throw std::runtime_error("expected diagnostic");}
struct Fixture{Memory memory;libc32::Heap heap{memory,16384,4096};libc32::AllocatorBridge bridge{memory,heap};
 std::uint32_t call(const char* name,std::uint32_t a=0,std::uint32_t b=0,jni32::ThreadId thread=1){auto binding=libc32::allocatorImport(name);check(bool(binding));std::array<std::uint32_t,16> r{};r[0]=a;r[1]=b;r[13]=0x8000;r[14]=0x10001;r[15]=binding->address;bool thumb=false;check(bridge.dispatch(r,thumb,thread)&&thumb&&r[15]==0x10000);return r[0];}
};
int main(){unsigned passed=0,failed=0;auto test=[&](const char* name,auto f){try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}};
 test("errno addresses are stable initialized and thread specific",[]{Fixture f;auto a=f.call("__errno"),b=f.call("__errno",0,0,2);check(a!=b&&!f.memory.readU32(a)&&!f.memory.readU32(b));f.memory.writeU32(a,37);check(f.call("__errno")==a&&f.memory.readU32(a)==37&&!f.memory.readU32(b));});
 test("successful malloc and calloc preserve errno",[]{Fixture f;auto cell=f.call("__errno");f.memory.writeU32(cell,37);auto a=f.call("malloc",24),b=f.call("calloc",3,8);check(a&&b&&!(a&7)&&!(b&7)&&f.memory.readU32(cell)==37);std::array<std::uint8_t,24> data{};f.memory.read(b,data);check(std::all_of(data.begin(),data.end(),[](auto v){return !v;}));});
 test("failed allocation and overflow set only caller ENOMEM",[]{Fixture f;auto a=f.call("__errno"),b=f.call("__errno",0,0,2);f.memory.writeU32(b,42);check(!f.call("malloc",0xffffffff)&&f.memory.readU32(a)==12&&f.memory.readU32(b)==42);f.memory.writeU32(a,0);check(!f.call("calloc",0xffffffff,16)&&f.memory.readU32(a)==12);});
 test("realloc and free zero policy preserve errno",[]{Fixture f;auto cell=f.call("__errno");auto at=f.call("malloc",24);f.memory.writeU32(cell,37);check(!f.call("realloc",at,0)&&!f.heap.stats().live_allocations&&f.memory.readU32(cell)==37);check(!f.call("free",0)&&f.memory.readU32(cell)==37);check(f.call("realloc",0,0));});
 test("failed realloc keeps original live allocation",[]{Fixture f;auto at=f.call("malloc",24);f.memory.writeU32(at,0xaabbccdd);check(!f.call("realloc",at,0xffffffff)&&f.heap.stats().live_allocations==1&&f.memory.readU32(at)==0xaabbccdd);f.call("free",at);check(!f.heap.stats().live_allocations);});
 test("errno storage cannot overlap arena or another thread",[]{Fixture f;f.memory.forced=16384;reject([&]{f.bridge.errnoPointer(1);});f.memory.forced=4096;check(f.bridge.errnoPointer(1)==4096);reject([&]{f.bridge.errnoPointer(2);});});
 test("invalid free traps",[]{Fixture f;auto at=f.call("malloc",8);f.call("free",at);reject([&]{f.call("free",at);});});
 test("ABI failures precede errno storage allocation",[]{Fixture f;std::array<std::uint32_t,16> r{};r[13]=0x8004;r[14]=0x10000;r[15]=libc32::allocator_imports[0].address;bool thumb=false;reject([&]{f.bridge.dispatch(r,thumb,1);});check(f.memory.next==4096);r[13]=0x8000;thumb=true;reject([&]{f.bridge.dispatch(r,thumb,1);});check(f.memory.next==4096);});
 test("unknown targets preserve architectural state",[]{Fixture f;std::array<std::uint32_t,16> r{};r[15]=0x10000;auto before=r;bool thumb=true;check(!f.bridge.dispatch(r,thumb,1)&&thumb&&r==before&&f.memory.next==4096);});
 std::cout<<"libc32 allocator bridge tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

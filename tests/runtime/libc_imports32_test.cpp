// SPDX-License-Identifier: Apache-2.0
#include "runtime/libc_imports32.hpp"
#include <algorithm>
#include <iostream>
using namespace rearm;
struct Memory:jni32::GuestMemory{
 std::array<std::uint8_t,1024> data{};
 std::uint32_t allocate(std::uint32_t,std::uint32_t)override{throw jni32::MemoryError("unexpected allocation");}
 void read(std::uint32_t at,std::span<std::uint8_t> out)const override{if(!at||std::uint64_t(at)+out.size()>data.size())throw jni32::MemoryError("read");std::copy_n(data.begin()+at,out.size(),out.begin());}
 void write(std::uint32_t at,std::span<const std::uint8_t> in)override{if(!at||std::uint64_t(at)+in.size()>data.size())throw jni32::MemoryError("write");std::copy(in.begin(),in.end(),data.begin()+at);}
};
void check(bool b){if(!b)throw std::runtime_error("check failed");}
template<class F>void reject(F f){try{f();}catch(const jni32::Error&){return;}throw std::runtime_error("missing diagnostic");}
int main(){unsigned passed=0,failed=0;auto test=[&](const char* name,auto f){try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}};
 Memory memory;std::array<std::uint32_t,16> r{};r[13]=0x8000;r[14]=0x10001;r[15]=libc32::import_base;r[0]=300;r[1]=100;r[2]=4;memory.data[100]=0xa5;
 test("lookup whitelist rejects unknown names and holes",[&]{for(auto binding:libc32::memory_imports)check(libc32::memoryImport(binding.name)->address==binding.address);check(!libc32::memoryImport("malloc"));check(!libc32::memoryImport(libc32::import_base+40));});
 test("unbound targets preserve CPU state",[&]{auto v=r;v[15]=0x10000;auto before=v;bool thumb=true;check(!libc32::dispatchMemory(v,thumb,memory)&&thumb&&v==before);});
 test("memcpy preserves registers and interworks on return",[&]{auto v=r;auto before=v;bool thumb=false;check(libc32::dispatchMemory(v,thumb,memory));check(thumb&&v[15]==0x10000&&v[0]==300&&memory.data[300]==0xa5);for(unsigned n=1;n<15;++n)check(v[n]==before[n]);});
 test("ARM return and unsigned memset byte",[&]{auto v=r;v[15]=libc32::import_base+8;v[14]=0x10000;v[1]=0x1ff;bool thumb=false;check(libc32::dispatchMemory(v,thumb,memory)&&!thumb&&memory.data[300]==255);});
 test("Thumb and odd callback targets fail before side effects",[&]{for(unsigned mode=0;mode<2;++mode){auto v=r;if(mode)v[15]|=1;bool thumb=!mode;auto before=v;auto bytes=memory.data;reject([&]{libc32::dispatchMemory(v,thumb,memory);});check(v==before&&bytes==memory.data);}});
 test("misaligned SP and return address reject",[&]{for(unsigned mode=0;mode<2;++mode){auto v=r;if(mode)v[14]=0x10002;else v[13]+=4;bool thumb=false;auto before=v;reject([&]{libc32::dispatchMemory(v,thumb,memory);});check(v==before);}});
 test("guest fault preserves callback CPU state",[&]{auto v=r;v[1]=1023;bool thumb=false;auto before=v;auto bytes=memory.data;reject([&]{libc32::dispatchMemory(v,thumb,memory);});check(v==before&&!thumb&&bytes==memory.data);});
 std::cout<<"libc32 import tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

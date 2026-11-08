// SPDX-License-Identifier: Apache-2.0
#include "runtime/aapcs32.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
using namespace rearm;
struct Memory: jni32::GuestMemory {
 std::vector<std::uint8_t> data=std::vector<std::uint8_t>(65536);std::uint32_t next=4096;
 std::uint32_t allocate(std::uint32_t n,std::uint32_t a)override{auto at=(next+a-1)&~(a-1);if(at+n>data.size())throw jni32::MemoryError("allocation");next=at+n;return at;}
 void read(std::uint32_t at,std::span<std::uint8_t> out)const override{if(std::uint64_t(at)+out.size()>data.size())throw jni32::MemoryError("read");std::copy_n(data.begin()+at,out.size(),out.begin());}
 void write(std::uint32_t at,std::span<const std::uint8_t> in)override{if(std::uint64_t(at)+in.size()>data.size())throw jni32::MemoryError("write");std::copy(in.begin(),in.end(),data.begin()+at);}
};
void check(bool v){if(!v)throw std::runtime_error("check failed");}
template<class F>void reject(F f){try{f();}catch(const jni32::Error&){return;}throw std::runtime_error("missing rejection");}
int main(){unsigned passed=0,failed=0;auto test=[&](const char* name,auto f){try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}};
 Memory memory;jni32::Host host(memory);auto env=host.attachThread(1);
 std::array<std::uint32_t,16> r{};r[13]=0x8000;r[14]=0x10001;
 auto at=[&](jni32::Function f){for(auto& b:host.bindings())if(b.function==f)return b.trampoline;throw std::runtime_error("binding");};
 test("register and stack words",[&]{r[0]=1;r[1]=2;r[2]=3;r[3]=4;memory.writeU32(r[13],5);memory.writeU32(r[13]+4,6);check(aapcs32::marshalWords(r,memory,6)==std::vector<std::uint32_t>({1,2,3,4,5,6}));});
 test("word limit",[&]{reject([&]{aapcs32::marshalWords(r,memory,65);});});
 test("SP alignment",[&]{auto v=r;v[13]+=4;reject([&]{aapcs32::marshalWords(v,memory,1);});});
 test("stack overflow",[&]{auto v=r;v[13]=0xfffffff8;reject([&]{aapcs32::marshalWords(v,memory,7);});});
 test("unmapped stack",[&]{auto v=r;v[13]=0x10000;reject([&]{aapcs32::marshalWords(v,memory,5);});});
 test("unrecognized target untouched",[&]{auto v=r;v[15]=0x1234;auto before=v;bool t=true;check(!aapcs32::dispatchJni(v,t,host,memory,1));check(v==before&&t);});
 test("GetVersion and Thumb return preserves registers",[&]{auto v=r;v[0]=env;v[15]=at(jni32::Function::GetVersion);auto before=v;bool t=false;check(aapcs32::dispatchJni(v,t,host,memory,1));check(v[0]==jni32::JNI_VERSION_1_6&&v[15]==0x10000&&t);for(unsigned n=1;n<15;++n)check(v[n]==before[n]);});
 test("ARM return",[&]{auto v=r;v[0]=env;v[14]=0x10000;v[15]=at(jni32::Function::GetVersion);bool t=false;check(aapcs32::dispatchJni(v,t,host,memory,1)&&!t&&v[15]==0x10000);});
 test("Thumb callback rejected",[&]{auto v=r;v[15]=at(jni32::Function::GetVersion);bool t=true;reject([&]{aapcs32::dispatchJni(v,t,host,memory,1);});});
 test("misaligned return rejected before side effects",[&]{auto v=r;v[15]=at(jni32::Function::GetVersion);v[14]=0x10002;bool t=false;auto before=v;reject([&]{aapcs32::dispatchJni(v,t,host,memory,1);});check(v==before&&!t);});
 test("unsupported slot diagnostic",[&]{auto v=r;v[15]=0xffe00000+7*4;bool t=false;reject([&]{aapcs32::dispatchJni(v,t,host,memory,1);});});
 test("GetEnv writes guest memory",[&]{auto v=r;v[0]=host.vmPointer();v[1]=0x9000;v[2]=jni32::JNI_VERSION_1_6;v[15]=at(jni32::Function::GetEnv);bool t=false;check(aapcs32::dispatchJni(v,t,host,memory,1));check(v[0]==0&&memory.readU32(0x9000)==env);});
 test("foreign env rejection preserves CPU",[&]{auto v=r;v[0]=123;v[15]=at(jni32::Function::GetVersion);auto before=v;bool t=false;reject([&]{aapcs32::dispatchJni(v,t,host,memory,1);});check(v==before);});
 std::cout<<"AAPCS32 tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

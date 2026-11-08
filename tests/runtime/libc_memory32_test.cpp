// SPDX-License-Identifier: Apache-2.0
#include "runtime/libc_memory32.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
using namespace rearm;
struct Memory: jni32::GuestMemory{
 std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(8192);
 std::uint32_t allocate(std::uint32_t,std::uint32_t)override{throw jni32::MemoryError("No test allocation");}
 void read(std::uint32_t at,std::span<std::uint8_t> out)const override{
  if(!at||std::uint64_t(at)+out.size()>bytes.size())throw jni32::MemoryError("unmapped read");
  std::copy_n(bytes.begin()+at,out.size(),out.begin());
 }
 void write(std::uint32_t at,std::span<const std::uint8_t> in)override{
  if(!at||std::uint64_t(at)+in.size()>bytes.size()||(at<6100&&std::uint64_t(at)+in.size()>6000))throw jni32::MemoryError("unmapped/protected write");
  std::copy(in.begin(),in.end(),bytes.begin()+at);
 }
};
void check(bool b){if(!b)throw std::runtime_error("check failed");}
template<class F>void reject(F f){try{f();}catch(const jni32::Error&){return;}throw std::runtime_error("expected error");}
int sign(int x){return (x>0)-(x<0);}
int main(){unsigned passed=0,failed=0;auto test=[&](const char* name,auto f){try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}};
 test("zero-size routines touch no guest address",[]{Memory m;libc32::MemoryServices s(m);check(s.copy(0,0,0)==0);check(s.fill(0,0,0)==0);check(s.compare(0,0,0)==0);check(s.boundedLength(0,0)==0);check(s.compareStrings(0,0,0)==0);check(s.copyString(0,0,0)==0);});
 test("limits reject without truncation",[]{Memory m;libc32::MemoryServices s(m,{32,16});reject([&]{s.fill(100,1,33);});reject([&]{s.boundedLength(100,17);});reject([&]{libc32::MemoryServices invalid(m,{16,32});});});
 test("null wrap unmapped and protected ranges",[]{Memory m;libc32::MemoryServices s(m);auto before=m.bytes;reject([&]{s.copy(100,0,4);});reject([&]{s.copy(0xfffffffe,100,4);});reject([&]{s.copy(100,8190,4);});reject([&]{s.fill(5998,9,4);});reject([&]{s.copy(5998,100,4);});check(m.bytes==before);});
 test("memcpy overlap diagnosed",[]{Memory m;libc32::MemoryServices s(m);reject([&]{s.copy(101,100,8);});reject([&]{s.copy(100,100,8);});});
 test("seeded differential memory routines, 1200 operations",[]{Memory m;libc32::MemoryServices s(m);std::uint32_t rng=0x6ca32;
  auto random=[&]{rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
  for(unsigned iteration=0;iteration<300;++iteration){for(auto& b:m.bytes)b=std::uint8_t(random());const auto n=random()%256;
   auto expected=m.bytes;std::memcpy(expected.data()+4000,expected.data()+1000,n);check(s.copy(4000,1000,n)==4000&&m.bytes==expected);
   const auto dst=1200+random()%40,src=1200+random()%40;expected=m.bytes;std::memmove(expected.data()+dst,expected.data()+src,n);check(s.copy(dst,src,n,true)==dst&&m.bytes==expected);
   const auto value=random();expected=m.bytes;std::memset(expected.data()+3000,value,n);check(s.fill(3000,value,n)==3000&&m.bytes==expected);
   check(sign(s.compare(1000,4000,n))==sign(std::memcmp(m.bytes.data()+1000,m.bytes.data()+4000,n)));
  }
 });
 test("unsigned comparison ordering",[]{Memory m;libc32::MemoryServices s(m);m.bytes[100]=0xff;m.bytes[200]=0x7f;check(s.compare(100,200,1)>0);check(s.compareStrings(100,200)>0);check(s.compareStrings(200,100,1)<0);});
 test("strings stop before unmapped bytes",[]{Memory m;libc32::MemoryServices s(m);m.bytes.back()=0;check(s.length(8191)==0);check(s.boundedLength(8191,20)==0);check(s.copyString(100,8191)==100&&m.bytes[100]==0);});
 test("unterminated and wrapping scans reject",[]{Memory m;libc32::MemoryServices s(m,{32,16});std::fill(m.bytes.begin()+100,m.bytes.begin()+116,1);reject([&]{s.length(100);});check(s.boundedLength(100,16)==16);reject([&]{s.length(0);});reject([&]{s.length(0xffffffff);});});
 test("strncpy pads and truncates",[]{Memory m;libc32::MemoryServices s(m);m.bytes[8190]='x';m.bytes[8191]=0;check(s.copyString(100,8190,8)==100);check(m.bytes[100]=='x');for(unsigned n=101;n<108;++n)check(!m.bytes[n]);m.bytes[200]='a';m.bytes[201]='b';m.bytes[202]='c';check(s.copyString(300,200,2)==300&&m.bytes[300]=='a'&&m.bytes[301]=='b');});
 test("string copies reject overlap and failed writes",[]{Memory m;libc32::MemoryServices s(m);m.bytes[100]='a';m.bytes[101]=0;auto before=m.bytes;reject([&]{s.copyString(101,100);});reject([&]{s.copyString(101,100,4);});reject([&]{s.copyString(6000,100);});check(m.bytes==before);});
 test("seeded differential strings, 500 operations",[]{Memory m;libc32::MemoryServices s(m);std::uint32_t rng=0xb1a32;
  for(unsigned i=0;i<100;++i){rng=rng*1664525+1013904223;auto n=rng%63;for(unsigned j=0;j<n;++j)m.bytes[100+j]=std::uint8_t(1+(rng+j)%254);m.bytes[100+n]=0;
   check(s.length(100)==std::strlen(reinterpret_cast<const char*>(m.bytes.data()+100)));check(s.boundedLength(100,n/2)==n/2);
   auto expected=m.bytes;std::strcpy(reinterpret_cast<char*>(expected.data()+500),reinterpret_cast<const char*>(expected.data()+100));s.copyString(500,100);check(expected==m.bytes);
   check(s.compareStrings(100,500)==0);m.bytes[500]=std::uint8_t(m.bytes[500]^0x80);check(sign(s.compareStrings(100,500))==sign(std::strcmp(reinterpret_cast<const char*>(m.bytes.data()+100),reinterpret_cast<const char*>(m.bytes.data()+500))));
  }
 });
 std::cout<<"libc32 memory tests: "<<passed<<" passed, "<<failed<<" failed; 1700 seeded differential operations\n";return failed?1:0;
}

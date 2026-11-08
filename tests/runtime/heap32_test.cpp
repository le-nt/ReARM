// SPDX-License-Identifier: Apache-2.0
#include "runtime/heap32.hpp"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>
using namespace rearm;
struct Memory:jni32::GuestMemory{
 std::uint32_t base;std::vector<std::uint8_t> bytes;bool fail_write=false;
 explicit Memory(std::uint32_t at=4096,std::uint32_t count=8192):base(at),bytes(count,0xa5){}
 std::uint32_t allocate(std::uint32_t,std::uint32_t)override{throw jni32::MemoryError("unexpected arena allocation");}
 void read(std::uint32_t at,std::span<std::uint8_t> out)const override{
  if(at<base||std::uint64_t(at)-base+out.size()>bytes.size())throw jni32::MemoryError("heap read");
  std::copy_n(bytes.begin()+(at-base),out.size(),out.begin());
 }
 void write(std::uint32_t at,std::span<const std::uint8_t> in)override{
  if(fail_write||at<base||std::uint64_t(at)-base+in.size()>bytes.size())throw jni32::MemoryError("heap write");
  std::copy(in.begin(),in.end(),bytes.begin()+(at-base));
 }
};
void check(bool b){if(!b)throw std::runtime_error("check failed");}
template<class F>void reject(F f){try{f();}catch(const jni32::Error&){return;}throw std::runtime_error("expected heap diagnostic");}
int main(){unsigned passed=0,failed=0;auto test=[&](const char* name,auto f){try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}};
 test("arena and metadata limits",[]{Memory m;for(auto [base,size]:std::vector<std::pair<unsigned,unsigned>>{{0,8},{4097,8},{4096,7},{0xfffffff8,16},{4096,128*1024*1024+8}})reject([&]{libc32::Heap h(m,base,size);});reject([&]{libc32::Heap h(m,4096,8192,0);});});
 test("alignment unique zero allocations and free null",[]{Memory m;libc32::Heap h(m,4096,8192);auto a=h.allocate(0),b=h.allocate(1),c=h.allocate(9);check(a&&b&&c&&a!=b&&b!=c&&!(a&7)&&!(b&7)&&!(c&7));h.release(0);check(h.stats().reserved_bytes==32);});
 test("exhaustion overflow and metadata quota",[]{Memory m;libc32::Heap h(m,4096,32,2);auto a=h.allocate(16),b=h.allocate(16);check(a&&b&&!h.allocate(1));h.release(a);check(!h.allocate(0xffffffff));check(h.allocate(8)==a);check(!h.allocate(1));h.release(b);check(h.stats().free_bytes==24);});
 test("coalescing restores full arena",[]{Memory m;libc32::Heap h(m,4096,64);auto a=h.allocate(16),b=h.allocate(16),c=h.allocate(32);h.release(b);h.release(a);check(h.stats().largest_free==32);h.release(c);check(h.stats().largest_free==64&&h.allocate(64)==4096);});
 test("invalid interior and double frees",[]{Memory m;libc32::Heap h(m,4096,64);auto a=h.allocate(16);reject([&]{h.release(a+1);});reject([&]{h.reallocate(a+8,4);});h.release(a);reject([&]{h.release(a);});check(h.stats().free_bytes==64);});
 test("calloc checked product and zero content",[]{Memory m;libc32::Heap h(m,4096,8192);check(!h.allocateZeroed(0xffffffff,8));auto at=h.allocateZeroed(3,7);check(at);std::vector<std::uint8_t> data(21);m.read(at,data);check(std::all_of(data.begin(),data.end(),[](auto b){return b==0;}));check(h.allocateZeroed(0,0));});
 test("calloc guest fault rolls back allocation",[]{Memory m;libc32::Heap h(m,4096,64);m.fail_write=true;reject([&]{h.allocateZeroed(4,8);});check(h.stats().free_bytes==64&&!h.stats().live_allocations);});
 test("realloc null zero shrink and adjacent growth",[]{Memory m;libc32::Heap h(m,4096,128);auto a=h.reallocate(0,32);std::vector<std::uint8_t> data(32,0x5a);m.write(a,data);check(h.reallocate(a,8)==a&&h.stats().reserved_bytes==8);check(h.reallocate(a,64)==a);std::vector<std::uint8_t> prefix(8);m.read(a,prefix);check(prefix==std::vector<std::uint8_t>(8,0x5a));check(!h.reallocate(a,0)&&h.stats().free_bytes==128);});
 test("moving realloc preserves content and failed growth retains old block",[]{Memory m;libc32::Heap h(m,4096,128);auto a=h.allocate(16),barrier=h.allocate(16);std::vector<std::uint8_t> data(16,0x37);m.write(a,data);auto moved=h.reallocate(a,48);check(moved&&moved!=a);std::vector<std::uint8_t> prefix(16);m.read(moved,prefix);check(prefix==data);check(!h.reallocate(moved,0xffffffff));m.read(moved,prefix);check(prefix==data);h.release(moved);h.release(barrier);check(h.stats().free_bytes==128);});
 test("moving realloc guest write fault retains old ownership",[]{Memory m;libc32::Heap h(m,4096,128);auto a=h.allocate(16),barrier=h.allocate(16);m.fail_write=true;reject([&]{h.reallocate(a,48);});check(h.stats().live_allocations==2&&h.stats().reserved_bytes==32);m.fail_write=false;h.release(a);h.release(barrier);check(h.stats().free_bytes==128);});
 test("arena may end exactly at 32-bit address-space boundary",[]{Memory m(0xffffffe0,32);libc32::Heap h(m,m.base,32);check(h.allocate(32)==m.base&&!h.allocate(1));h.release(m.base);check(h.stats().free_bytes==32);});
 test("1000 seeded allocation traces preserve data and disjoint ownership",[]{Memory m;libc32::Heap h(m,4096,8192);struct Live{std::uint32_t at,size;std::uint8_t value;};std::vector<Live> live;std::uint32_t rng=0x32a110c;
  auto random=[&]{rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
  for(unsigned i=0;i<1000;++i){auto action=random()%3;
   if(live.empty()||action==0){auto size=1+random()%128,at=h.allocate(size);if(at){auto value=std::uint8_t(random());m.write(at,std::vector<std::uint8_t>(size,value));live.push_back({at,size,value});}}
   else{auto index=random()%live.size();auto old=live[index];if(action==1){h.release(old.at);live.erase(live.begin()+index);}else{auto size=1+random()%256,at=h.reallocate(old.at,size);auto preserved=at?std::min(size,old.size):old.size;std::vector<std::uint8_t> data(preserved);m.read(at?at:old.at,data);check(data==std::vector<std::uint8_t>(preserved,old.value));if(at){m.write(at,std::vector<std::uint8_t>(size,old.value));live[index]={at,size,old.value};}}}
   std::sort(live.begin(),live.end(),[](auto a,auto b){return a.at<b.at;});std::uint64_t reserved=0;
   for(std::size_t n=0;n<live.size();++n){auto count=(live[n].size+7)&~7u;reserved+=count;if(n+1<live.size())check(std::uint64_t(live[n].at)+count<=live[n+1].at);}
   auto stats=h.stats();check(stats.live_allocations==live.size()&&stats.reserved_bytes==reserved&&stats.free_bytes+reserved==8192);
  }
  for(auto b:live)h.release(b.at);check(h.stats().largest_free==8192);
 });
 test("eight host threads allocate and release disjoint blocks",[]{Memory m;libc32::Heap h(m,4096,8192);std::atomic<unsigned> errors=0;std::vector<std::thread> workers;for(unsigned n=0;n<8;++n)workers.emplace_back([&,n]{try{for(unsigned j=0;j<100;++j){auto at=h.allocate(32);check(at);std::vector<std::uint8_t> data(32,std::uint8_t(n));m.write(at,data);std::vector<std::uint8_t> read(32);m.read(at,read);check(read==data);h.release(at);}}catch(...){++errors;}});for(auto& worker:workers)worker.join();check(!errors&&h.stats().free_bytes==8192);});
 std::cout<<"heap32 tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

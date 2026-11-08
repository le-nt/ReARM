// SPDX-License-Identifier: Apache-2.0
#include "emitter.hpp"
#include <ghidra/opcodes.hh>
#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace rearm::emit {
namespace {
using lift::Varnode;
std::string number(std::uint64_t n) { return std::to_string(n)+"ULL"; }
std::string location(const lift::Instruction& i) { std::ostringstream s; s<<"guest 0x"<<std::hex<<i.address<<(i.thumb?" Thumb":" ARM");return s.str(); }
class InstructionEmitter {
 std::ostream& o;
 const lift::Instruction& ins;
 Varnode thumb_register;
 std::map<std::uint64_t,std::uint32_t> temps;
 std::string label(std::size_t n) const {return "i"+std::to_string(ins.address)+(ins.thumb?"t":"a")+"p"+std::to_string(n);}
 std::string done() const {return label(ins.ops.size()+1);}
 public:
 InstructionEmitter(std::ostream& out,const lift::Instruction& i,Varnode tb):o(out),ins(i),thumb_register(std::move(tb)) {
   for(const auto& op:i.ops) {
     auto add=[&](const Varnode& v){if(v.space=="unique") for(unsigned j=0;j<v.size;++j) temps.emplace(v.offset+j,0);};
     if(op.output)add(*op.output);for(const auto& v:op.inputs)add(v);
   }
   std::uint32_t n=0;for(auto& [_,slot]:temps)slot=n++;
 }
 std::string read(const Varnode& v) {
   if(v.size==0||v.size>8)throw std::runtime_error(location(ins)+": unsupported varnode width "+std::to_string(v.size));
   if(v.space=="const")return "("+number(v.offset)+"&mask("+std::to_string(v.size)+"))";
   if(v.space=="register")return "rd(reg,"+number(v.offset)+","+std::to_string(v.size)+")";
   if(v.space=="unique") {
     // Compact byte mapping preserves all partial/overlapping temporary aliases.
     std::string s="(";for(unsigned j=0;j<v.size;++j){if(j)s+="|";s+="(uint64_t(tmp["+std::to_string(temps.at(v.offset+j))+"])<<"+std::to_string(8*j)+")";}return s+")";
   }
   if(v.space=="ram")return "load("+number(v.offset)+","+std::to_string(v.size)+")";
   throw std::runtime_error(location(ins)+": unsupported varnode space "+v.space);
 }
 void write(const Varnode& v,const std::string& expr) {
   if(v.size==0||v.size>8)throw std::runtime_error(location(ins)+": unsupported output width");
   o<<"value="<<expr<<";\n";
   if(v.space=="register")o<<"wr(reg,"<<v.offset<<","<<v.size<<",value);\n";
   else if(v.space=="unique")for(unsigned j=0;j<v.size;++j)o<<"tmp["<<temps.at(v.offset+j)<<"]=uint8_t(value>>"<<j*8<<");\n";
   else if(v.space=="ram")o<<"store("<<v.offset<<","<<v.size<<",value);\n";
   else throw std::runtime_error(location(ins)+": invalid output space "+v.space);
 }
 void jump(const Varnode& target,std::size_t index) {
   if(target.space=="const") {
     auto delta=static_cast<std::int64_t>(target.offset);
     auto dest=static_cast<std::int64_t>(index)+delta;
     if(dest<0||dest>static_cast<std::int64_t>(ins.ops.size()))throw std::runtime_error(location(ins)+": p-code branch outside instruction");
     o<<"goto "<<label(dest)<<";\n";
   }else {o<<"next=uint32_t("<<(target.space=="ram"?number(target.offset):read(target))<<")&~1u; goto "<<done()<<";\n";}
 }
 void run() {
   using namespace ghidra;
   o<<"{ uint8_t tmp["<<std::max<std::size_t>(1,temps.size())<<"]{}; uint64_t value=0; uint32_t next="<<ins.address+ins.length<<"u;\n";
   for(std::size_t k=0;k<ins.ops.size();++k) {
     const auto& op=ins.ops[k];o<<label(k)<<":; // "<<get_opname(static_cast<OpCode>(op.opcode))<<"\n";
     auto a=[&](unsigned n){if(n>=op.inputs.size())throw std::runtime_error(location(ins)+": missing p-code operand");return read(op.inputs[n]);};
     auto bits=[&](unsigned n){return std::to_string(op.inputs.at(n).size*8);};
     std::string expr;
     switch(op.opcode) {
      case CPUI_COPY:case CPUI_CAST:case CPUI_INT_ZEXT:expr=a(0);break;
      case CPUI_INT_SEXT:expr="uint64_t(signed_value("+a(0)+","+bits(0)+"))";break;
      case CPUI_LOAD:expr="load(uint32_t("+a(1)+"),"+std::to_string(op.output->size)+")";break;
      case CPUI_STORE:o<<"store(uint32_t("<<a(1)<<"),"<<op.inputs.at(2).size<<","<<a(2)<<");\n";continue;
      case CPUI_INT_ADD:expr=a(0)+"+"+a(1);break;
      case CPUI_INT_SUB:expr=a(0)+"-"+a(1);break;
      case CPUI_INT_MULT:expr=a(0)+"*"+a(1);break;
      case CPUI_INT_AND:expr=a(0)+"&"+a(1);break;
      case CPUI_INT_OR:expr=a(0)+"|"+a(1);break;
      case CPUI_INT_XOR:expr=a(0)+"^"+a(1);break;
      case CPUI_INT_NEGATE:expr="~"+a(0);break;
      case CPUI_INT_2COMP:expr="uint64_t(0)-"+a(0);break;
      case CPUI_BOOL_NEGATE:expr="!"+a(0);break;
      case CPUI_BOOL_AND:expr="bool("+a(0)+")&&bool("+a(1)+")";break;
      case CPUI_BOOL_OR:expr="bool("+a(0)+")||bool("+a(1)+")";break;
      case CPUI_BOOL_XOR:expr="bool("+a(0)+")!=bool("+a(1)+")";break;
      case CPUI_INT_EQUAL:expr=a(0)+"=="+a(1);break;
      case CPUI_INT_NOTEQUAL:expr=a(0)+"!="+a(1);break;
      case CPUI_INT_LESS:expr=a(0)+"<"+a(1);break;
      case CPUI_INT_LESSEQUAL:expr=a(0)+"<="+a(1);break;
      case CPUI_INT_SLESS:expr="signed_value("+a(0)+","+bits(0)+")<signed_value("+a(1)+","+bits(1)+")";break;
      case CPUI_INT_SLESSEQUAL:expr="signed_value("+a(0)+","+bits(0)+")<=signed_value("+a(1)+","+bits(1)+")";break;
      case CPUI_INT_LEFT:expr="shift_left("+a(0)+","+a(1)+","+bits(0)+")";break;
      case CPUI_INT_RIGHT:expr="shift_right("+a(0)+","+a(1)+","+bits(0)+",false)";break;
      case CPUI_INT_SRIGHT:expr="shift_right("+a(0)+","+a(1)+","+bits(0)+",true)";break;
      case CPUI_INT_CARRY:expr="carry("+a(0)+","+a(1)+","+bits(0)+")";break;
      case CPUI_INT_SCARRY:expr="overflow("+a(0)+","+a(1)+","+bits(0)+",false)";break;
      case CPUI_INT_SBORROW:expr="overflow("+a(0)+","+a(1)+","+bits(0)+",true)";break;
      case CPUI_INT_DIV:expr="divide("+a(0)+","+a(1)+","+bits(0)+",false,false)";break;
      case CPUI_INT_REM:expr="divide("+a(0)+","+a(1)+","+bits(0)+",false,true)";break;
      case CPUI_INT_SDIV:expr="divide("+a(0)+","+a(1)+","+bits(0)+",true,false)";break;
      case CPUI_INT_SREM:expr="divide("+a(0)+","+a(1)+","+bits(0)+",true,true)";break;
      case CPUI_PIECE:if(op.inputs.at(1).size>=8)throw std::runtime_error("PIECE exceeds 64 bits");expr="("+a(0)+"<<"+bits(1)+")|"+a(1);break;
      case CPUI_SUBPIECE:expr="shift_right("+a(0)+","+a(1)+"*8,64,false)";break;
      case CPUI_POPCOUNT:expr="std::popcount("+a(0)+")";break;
      case CPUI_LZCOUNT:expr="std::countl_zero("+a(0)+")-(64-"+bits(0)+")";break;
      case CPUI_BRANCH:case CPUI_CALL:case CPUI_BRANCHIND:case CPUI_CALLIND:case CPUI_RETURN:jump(op.inputs.at(0),k);continue;
      case CPUI_CBRANCH:o<<"if("<<a(1)<<") {\n";jump(op.inputs.at(0),k);o<<"}\n";continue;
      case CPUI_CALLOTHER:
        if(op.userop=="setISAMode") {write(thumb_register,a(1));continue;}
        throw std::runtime_error(location(ins)+": unsupported userop "+op.userop);
      default:throw std::runtime_error(location(ins)+": unsupported p-code "+get_opname(static_cast<OpCode>(op.opcode)));
     }
     if(!op.output)throw std::runtime_error(location(ins)+": missing p-code output");
     write(*op.output,"("+expr+")");
   }
   o<<label(ins.ops.size())<<":; "<<done()<<":; pc=next; break; }\n";
 }
};
}

void cpp(std::ostream& o,const elf::Image& image,const lift::Program& program,std::uint32_t entry,
 const std::vector<std::pair<std::uint32_t,std::uint32_t>>& observed,std::uint64_t execution_limit,
 const std::vector<std::uint32_t>& initializers,bool jni_onload,const JniConfig& jni,bool libc_memory,bool libc_heap,bool instruction_functions) {
 validateJniConfig(jni);
 if(libc_heap&&!libc_memory)throw std::runtime_error("Heap profile requires memory import profile");
 auto reg=[&](const std::string& name){auto it=program.registers.find(name);if(it==program.registers.end())throw std::runtime_error("SLA lacks register "+name);return it->second;};
 std::size_t register_bytes=0;for(const auto& [_,v]:program.registers)register_bytes=std::max<std::size_t>(register_bytes,v.offset+v.size);
 if(jni_onload)o<<"#include \"runtime/aapcs32.hpp\"\n";
 if(libc_memory)o<<"#include \"runtime/libc_imports32.hpp\"\n";
 if(libc_heap)o<<"#include \"runtime/libc_allocator32.hpp\"\n";
 o<<R"CPP(// Generated by ReARM: statically lowered SLEIGH operations; no runtime decoder.
#include <array>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using std::uint8_t; using std::uint32_t; using std::uint64_t; using std::int64_t;
static uint64_t mask(unsigned bytes){return bytes==8?~uint64_t(0):((uint64_t(1)<<(bytes*8))-1);}
template<class T> static uint64_t rd(const T& b,uint64_t at,unsigned n){if(at>b.size()||n>b.size()-at)throw std::runtime_error("register range");uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[at+i])<<(i*8);return v;}
template<class T> static void wr(T& b,uint64_t at,unsigned n,uint64_t v){if(at>b.size()||n>b.size()-at)throw std::runtime_error("register range");for(unsigned i=0;i<n;++i)b[at+i]=uint8_t(v>>(i*8));}
static int64_t signed_value(uint64_t v,unsigned bits){if(bits<64){auto s=uint64_t(1)<<(bits-1);v=(v^s)-s;}return std::bit_cast<int64_t>(v);}
static uint64_t shift_left(uint64_t v,uint64_t n,unsigned bits){return n>=bits?0:v<<n;}
static uint64_t shift_right(uint64_t v,uint64_t n,unsigned bits,bool sign){if(n>=bits)return sign&&(v>>(bits-1))?mask(bits/8):0;if(!sign||!(v>>(bits-1)))return v>>n;if(!n)return v;return (v>>n)|(~uint64_t(0)<<(bits-n));}
static uint64_t carry(uint64_t a,uint64_t b,unsigned bits){return bits==64?a+b<a:(a+b)>>bits;}
static uint64_t overflow(uint64_t a,uint64_t b,unsigned bits,bool sub){auto r=sub?a-b:a+b;auto sign=uint64_t(1)<<(bits-1);return ((sub?(a^b):~(a^b))&(a^r)&sign)!=0;}
static uint64_t divide(uint64_t a,uint64_t b,unsigned bits,bool sign,bool rem){if(!b)throw std::runtime_error("guest divide by zero");if(!sign)return rem?a%b:a/b;auto x=signed_value(a,bits),y=signed_value(b,bits);if(x==std::numeric_limits<int64_t>::min()&&y==-1)return rem?0:a;return uint64_t(rem?x%y:x/y);}
struct Region{uint32_t address; unsigned flags; std::vector<uint8_t> bytes;};
static std::vector<Region> memory;
static uint8_t& byte(uint32_t address,bool write){for(auto& r:memory)if(uint64_t(address)>=r.address&&uint64_t(address)-r.address<r.bytes.size()){if(write&&!(r.flags&2))throw std::runtime_error("guest write permission");return r.bytes[address-r.address];}throw std::runtime_error("unmapped guest memory");}
static uint64_t load(uint32_t address,unsigned n){if(uint64_t(address)+n>0x100000000ULL)throw std::runtime_error("guest address overflow");uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(byte(address+i,false))<<(i*8);return v;}
static void store(uint32_t address,unsigned n,uint64_t v){if(uint64_t(address)+n>0x100000000ULL)throw std::runtime_error("guest address overflow");for(unsigned i=0;i<n;++i)(void)byte(address+i,true);for(unsigned i=0;i<n;++i)byte(address+i,true)=uint8_t(v>>(i*8));}
)CPP";
 if(jni_onload||libc_memory)o<<R"CPP(
class JniMemory final:public rearm::jni32::GuestMemory {
 uint32_t next=0x70000000u;
public:
 uint32_t allocate(uint32_t size,uint32_t alignment) override {
  if(!alignment||(alignment&(alignment-1)))throw rearm::jni32::MemoryError("JNI allocation alignment");
  uint64_t at=(uint64_t(next)+alignment-1)&~uint64_t(alignment-1);
  if(!size||at+size>0x70100000ULL)throw rearm::jni32::MemoryError("JNI arena exhausted");
  next=uint32_t(at+size);return uint32_t(at);
 }
 void read(uint32_t at,std::span<uint8_t> out)const override {
  if(uint64_t(at)+out.size()>0x100000000ULL)throw rearm::jni32::MemoryError("JNI read overflow");
  for(std::size_t n=0;n<out.size();++n)out[n]=uint8_t(load(at+uint32_t(n),1));
 }
 void write(uint32_t at,std::span<const uint8_t> in)override {
  if(uint64_t(at)+in.size()>0x100000000ULL)throw rearm::jni32::MemoryError("JNI write overflow");
  for(std::size_t n=0;n<in.size();++n)(void)byte(at+uint32_t(n),true);
  for(std::size_t n=0;n<in.size();++n)store(at+uint32_t(n),1,in[n]);
 }
};
)CPP";
 auto function_name=[](const lift::Instruction& i){return "step_"+std::to_string(i.address)+(i.thumb?"t":"a");};
 if(instruction_functions)for(const auto& i:program.instructions){
  o<<"__attribute__((noinline)) static uint32_t "<<function_name(i)<<"(std::array<uint8_t,"<<register_bytes<<">& reg){uint32_t pc=0;do{\n";
  InstructionEmitter(o,i,reg("TB")).run();o<<"}while(false);return pc;}\n";
 }
 o<<"int main(int argc,char** argv){try{\nmemory.clear();\n";
 for(const auto& s:image.segments()) {
   // BSS is allocated at runtime, never expanded into millions of C++ zero tokens.
   auto initialized=s.bytes.size();
   while(initialized && !s.bytes[initialized-1])--initialized;
   o<<"memory.push_back({"<<s.address<<"u,"<<s.flags<<"u,std::vector<uint8_t>("<<s.bytes.size()<<")});\n";
   if(initialized) {
     o<<"{ static const uint8_t initial[]={";
     for(std::size_t n=0;n<initialized;++n)o<<unsigned(s.bytes[n])<<",";
     o<<"};std::copy(std::begin(initial),std::end(initial),memory.back().bytes.begin());}\n";
   }
 }
 if(jni_onload||libc_heap)o<<R"CPP(
for(const auto& r:memory){
 if(uint64_t(r.address)<0x70100000ULL&&uint64_t(r.address)+r.bytes.size()>0x70000000ULL)
  throw std::runtime_error("ELF overlaps JNI data arena");
 if(uint64_t(r.address)<0xffe02000ULL&&uint64_t(r.address)+r.bytes.size()>0xffe00000ULL)
  throw std::runtime_error("ELF overlaps JNI callback arena");
}
memory.push_back({0x70000000u,6,std::vector<uint8_t>(1048576)});
)CPP";
 if(jni_onload||libc_memory)o<<"JniMemory jni_memory;\n";
 if(jni_onload)o<<"rearm::jni32::Host jni_host(jni_memory);jni_host.attachThread(1);\n";
 if(libc_heap)o<<"memory.push_back({0x60000000u,6,std::vector<uint8_t>(16777216)});rearm::libc32::Heap guest_heap(jni_memory,0x60000000u,16777216);rearm::libc32::AllocatorBridge guest_allocator(jni_memory,guest_heap);\n";
 // Configuration validation permits only C++ literal-safe ASCII declaration bytes.
 for(const auto& c:jni.classes)o<<"jni_host.registerClass(\""<<c<<"\");\n";
 for(const auto& m:jni.methods)o<<"jni_host.registerMethod(\""<<m.class_name<<"\",\""<<m.name<<"\",\""<<m.signature<<"\","<<(m.is_static?"true":"false")<<",true);\n";
 o<<"memory.push_back({0x7fef0000u,6,std::vector<uint8_t>(65536)});\nstd::array<uint8_t,"<<register_bytes<<"> reg{};\n";
 auto setreg=[&](const std::string& n,const std::string& value){auto v=reg(n);o<<"wr(reg,"<<v.offset<<","<<v.size<<","<<value<<");\n";};
 o<<"const uint32_t calls[]={";
 for(auto at:initializers)o<<at<<"u,";
 o<<entry<<"u};uint64_t steps=0;uint32_t pc=0;\nfor(std::size_t call=0;call<std::size(calls)+"<<jni.invoke.size()<<";++call){\nuint32_t target=call<std::size(calls)?calls[call]:0;\n";
 if(!jni.invoke.empty()){
  const auto& m=jni.invoke[0];
  o<<"if(call==std::size(calls)){\n";
  auto result=reg("r0");
  o<<"if(rd(reg,"<<result.offset<<","<<result.size<<")!=0x10006u||jni_host.pendingException(1))throw std::runtime_error(\"JNI_OnLoad did not accept JNI 1.6 cleanly\");\n";
  o<<"auto binding=jni_host.lookupNative(\""<<m.class_name<<"\",\""<<m.name<<"\",\""<<m.signature<<"\");if(!binding||!binding->is_static)throw std::runtime_error(\"requested static native method was not registered\");target=binding->guest_entry;}\n";
 }
 o<<"reg.fill(0);\n";
 setreg("r0",jni_onload?"call+1==std::size(calls)?jni_host.vmPointer():0":"call+1==std::size(calls)&&argc>1?uint32_t(std::strtoull(argv[1],nullptr,0)):0");setreg("sp","0x7ff00000u");setreg("lr","0xffff0000u");setreg("TB","target&1u");
 if(!jni.invoke.empty()){
  const auto& m=jni.invoke[0];const auto count=m.signature.find(')')-1;
  o<<"if(call==std::size(calls)){jni_host.beginNativeCall(1);\n";setreg("r0","jni_host.envPointer(1)");
  o<<"auto receiver=jni_host.classHandle(1,\""<<m.class_name<<"\");if(!receiver)throw std::runtime_error(\"JNI native receiver allocation failed\");\n";
  setreg("r1","receiver");
  // Align public-interface SP to 8 bytes, including outgoing stack words.
  const auto stack_bytes=((count>2?(count-2)*4:0)+7)&~std::size_t(7);
  setreg("sp",std::to_string(0x7ff00000u-stack_bytes)+"u");
  for(std::size_t n=0;n<count;++n){auto value="argc>"+std::to_string(n+1)+"?uint32_t(std::strtoull(argv["+std::to_string(n+1)+"],nullptr,0)):0";
   if(n<2)setreg("r"+std::to_string(n+2),value);
   else o<<"store("<<0x7ff00000u-stack_bytes+(n-2)*4<<"u,4,"<<value<<");\n";
  }
  o<<"}\n";
 }
 o<<"pc=target&~1u;\nwhile(pc!=0xffff0000u){if(++steps>"<<execution_limit<<"ULL)throw std::runtime_error(\"guest execution budget exhausted\");\n";
 setreg("pc","pc");
 if(jni_onload||libc_memory){
  o<<"{std::array<uint32_t,16> abi{};\n";
  for(unsigned n=0;n<16;++n){auto v=reg(n==13?"sp":n==14?"lr":n==15?"pc":"r"+std::to_string(n));o<<"abi["<<n<<"]=uint32_t(rd(reg,"<<v.offset<<","<<v.size<<"));\n";}
  o<<"bool thumb=rd(reg,"<<reg("TB").offset<<",1)!=0; if(";
  if(jni_onload)o<<"rearm::aapcs32::dispatchJni(abi,thumb,jni_host,jni_memory,1)";
  if(jni_onload&&libc_memory)o<<"||";
  if(libc_memory)o<<"rearm::libc32::dispatchMemory(abi,thumb,jni_memory)";
  if(libc_heap)o<<"||guest_allocator.dispatch(abi,thumb,1)";
  o<<"){\n";
  setreg("r0","abi[0]");setreg("TB","thumb");o<<"pc=abi[15];continue;}}\n";
 }
 o<<"switch(uint64_t(pc)|(rd(reg,"<<reg("TB").offset<<",1)?0x100000000ULL:0)){\n";
 for(const auto& i:program.instructions){
  o<<"case "<<number(std::uint64_t(i.address)|(i.thumb?0x100000000ULL:0))<<":\n";
  if(instruction_functions)o<<"pc="<<function_name(i)<<"(reg);break;\n";
  else InstructionEmitter(o,i,reg("TB")).run();
 }
 o<<"default:std::cerr<<\"unknown guest target 0x\"<<std::hex<<pc<<'\\n';throw std::runtime_error(\"unknown guest target: rebuild with discovery hints\");}}\n";
 if(!jni.invoke.empty())o<<"if(call==std::size(calls))jni_host.endNativeCall(1);\n";
 o<<"}\n";
 setreg("pc","pc");
 o<<"uint32_t cpsr=0x10u;\n";
 for(const auto& [name,bit]:std::vector<std::pair<std::string,int>>{{"NG",31},{"ZR",30},{"CY",29},{"OV",28},{"TB",5}}){auto v=reg(name);o<<"cpsr|=uint32_t(rd(reg,"<<v.offset<<","<<v.size<<")!=0)<<"<<bit<<";\n";}
 o<<"std::cout<<\"{\\\"registers\\\":[\";\n";
 for(unsigned n=0;n<16;++n){auto v=reg(n==13?"sp":n==14?"lr":n==15?"pc":"r"+std::to_string(n));if(n)o<<"std::cout<<',';\n";o<<"std::cout<<rd(reg,"<<v.offset<<","<<v.size<<");\n";}
 o<<"std::cout<<\"],\\\"cpsr\\\":\"<<cpsr<<\",\\\"memory\\\":[\";\n";
 for(std::size_t k=0;k<observed.size();++k){auto [addr,size]=observed[k];if(k)o<<"std::cout<<',';\n";o<<"std::cout<<\"{\\\"address\\\":"<<addr<<",\\\"bytes\\\":\\\"\";for(unsigned j=0;j<"<<size<<";++j){char h[3];std::snprintf(h,sizeof(h),\"%02x\",unsigned(load("<<addr<<"u+j,1)));std::cout<<h;}std::cout<<\"\\\"}\";\n";}
 o<<"std::cout<<\"]}\\n\";return 0;}catch(const std::exception& e){std::cerr<<\"ReARM runtime: \"<<e.what()<<'\\n';return 1;}}\n";
}
}

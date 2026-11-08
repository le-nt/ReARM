// SPDX-License-Identifier: Apache-2.0
#include "elf/elf_image.hpp"
#include "lift/lifter.hpp"
#include "emit/emitter.hpp"
#include "ir/verify.hpp"
#include "link/linker.hpp"
#include "runtime/libc_allocator32.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
int main(int argc,char** argv){try{
 std::map<std::string,std::string> args;
 std::vector<std::string> dependencies;
 rearm::emit::JniConfig jni;
 for(int i=1;i<argc;i+=2){if(i+1==argc)throw std::runtime_error("option requires value");auto key=std::string(argv[i]);if(key=="--dependency")dependencies.push_back(argv[i+1]);else if(key=="--jni-class")jni.classes.push_back(argv[i+1]);else if(key=="--jni-native")jni.methods.push_back(rearm::emit::parseJniMethod(argv[i+1]));else if(key=="--jni-invoke")jni.invoke.push_back(rearm::emit::parseJniMethod(argv[i+1],true));else if(!args.emplace(key,argv[i+1]).second)throw std::runtime_error("duplicate option: "+key);}
 for(auto key:{"--input","--sla","--output"})if(!args.contains(key))throw std::runtime_error(std::string("required option: ")+key);
 for(const auto& [key,_]:args)if(key!="--input"&&key!="--sla"&&key!="--output"&&key!="--entry"&&key!="--observe"&&key!="--max-instructions"&&key!="--load-bias"&&key!="--dispatch"&&key!="--initialize"&&key!="--jni"&&key!="--runtime"&&key!="--codegen")throw std::runtime_error("unknown option: "+key);
 const bool instruction_functions=args.contains("--codegen")&&args.at("--codegen")=="functions";
 if(args.contains("--codegen")&&!instruction_functions&&args.at("--codegen")!="monolithic")throw std::runtime_error("--codegen expects monolithic or functions");
 const bool libc_heap=args.contains("--runtime")&&args.at("--runtime")=="libc-basic";
 const bool libc_memory=libc_heap||(args.contains("--runtime")&&args.at("--runtime")=="libc-memory");
 if(args.contains("--runtime")&&!libc_memory)throw std::runtime_error("--runtime expects libc-memory or libc-basic");
 const bool jni_onload=args.contains("--jni")&&args.at("--jni")=="onload";
 if(args.contains("--jni")&&!jni_onload)throw std::runtime_error("--jni expects onload");
 rearm::emit::validateJniConfig(jni);
 if(!jni_onload&&(!jni.classes.empty()||!jni.methods.empty()||!jni.invoke.empty()))throw std::runtime_error("JNI declarations require --jni onload");
 if(args.contains("--dispatch")&&args.at("--dispatch")!="iterative")throw std::runtime_error("unsupported dispatch mode: "+args.at("--dispatch")+" (available: iterative)");
 const auto start=std::chrono::steady_clock::now();
 std::uint32_t bias=0;
 if(args.contains("--load-bias")) {
   const auto& text=args.at("--load-bias");std::size_t consumed=0;
   auto value=std::stoull(text,&consumed,0);
   if(consumed!=text.size()||value>0xffffffffULL||text.front()=='-')throw std::runtime_error("invalid load bias: "+text);
   bias=static_cast<std::uint32_t>(value);
 }
 std::vector<rearm::link::ModuleInput> modules{{std::filesystem::path(args.at("--input")).filename().string(),args.at("--input"),bias}};
 for(const auto& dependency:dependencies){auto split=dependency.rfind('@');if(split==std::string::npos)throw std::runtime_error("--dependency requires PATH@LOAD_BIAS");auto path=dependency.substr(0,split);auto text=dependency.substr(split+1);std::size_t used=0;auto at=std::stoull(text,&used,0);if(path.empty()||used!=text.size()||at>0xffffffffULL)throw std::runtime_error("invalid dependency layout: "+dependency);modules.push_back({std::filesystem::path(path).filename().string(),path,static_cast<std::uint32_t>(at)});}
 rearm::elf::Image::Resolver external;
 std::vector<std::string> external_modules;std::vector<std::uint32_t> host_entries;
 if(libc_memory){
  external_modules.push_back("libc.so");
  for(auto binding:rearm::libc32::memory_imports)host_entries.push_back(binding.address);
  if(libc_heap)for(auto binding:rearm::libc32::allocator_imports)host_entries.push_back(binding.address);
  external=[libc_heap](const rearm::elf::Symbol& symbol)->std::optional<std::uint32_t>{
   auto binding=rearm::libc32::memoryImport(symbol.name);
   if(!binding&&libc_heap)binding=rearm::libc32::allocatorImport(symbol.name);
   if(binding){
    if(symbol.type!=0&&symbol.type!=2)throw rearm::elf::Error("libc-memory import is not a function: "+symbol.name);
    return binding->address;
   }return {};
  };
 }
 auto linked=rearm::link::link(std::move(modules),external,external_modules);
 auto image=std::move(linked.image);
 for(const auto& s:image.segments()){
  const auto end=std::uint64_t(s.address)+s.bytes.size();
  if(std::uint64_t(s.address)<0x7ff00000ULL&&end>0x7fef0000ULL)throw rearm::elf::Error("ELF overlaps reserved guest stack");
  if(std::uint64_t(s.address)<0xffff1000ULL&&end>0xffff0000ULL)throw rearm::elf::Error("ELF overlaps reserved return sentinel");
 }
 if(libc_memory)for(const auto& s:image.segments())if(std::uint64_t(s.address)<0xffd01000ULL&&std::uint64_t(s.address)+s.bytes.size()>0xffd00000ULL)throw rearm::elf::Error("ELF overlaps libc-memory callback arena");
 if(libc_heap)for(const auto& s:image.segments())if((std::uint64_t(s.address)<0x61000000ULL&&std::uint64_t(s.address)+s.bytes.size()>0x60000000ULL)||(std::uint64_t(s.address)<0x70100000ULL&&std::uint64_t(s.address)+s.bytes.size()>0x70000000ULL))throw rearm::elf::Error("ELF overlaps libc-basic heap/errno arena");
 if(args.contains("--initialize")){if(args.at("--initialize")=="no")linked.constructors.clear();else if(args.at("--initialize")!="yes")throw std::runtime_error("--initialize expects yes or no");}
 auto address=[&](const std::string& value){if(auto s=image.find_symbol(value))return s->value;std::size_t n=0;auto v=std::stoull(value,&n,0);if(n!=value.size()||v>0xffffffffULL)throw std::runtime_error("invalid guest address: "+value);return static_cast<std::uint32_t>(v);};
 auto entry=args.contains("--entry")?address(args.at("--entry")):image.entry();
 std::vector<std::pair<std::uint32_t,std::uint32_t>> observed;
 if(args.contains("--observe")){auto v=args.at("--observe");auto split=v.find(':');if(split==std::string::npos)throw std::runtime_error("--observe expects symbol:size");auto size=std::stoull(v.substr(split+1),nullptr,0);if(size>1048576)throw std::runtime_error("observation limit exceeded");auto addr=address(v.substr(0,split));if(!image.mapped(addr,size))throw std::runtime_error("observation not mapped");observed.emplace_back(addr,static_cast<std::uint32_t>(size));}
 auto limit=args.contains("--max-instructions")?std::stoull(args.at("--max-instructions")):1000000ULL;
 if(!limit||limit>10000000)throw std::runtime_error("instruction limit must be 1..10000000");
 auto program=rearm::lift::lift(image,args.at("--sla"),entry,static_cast<std::uint32_t>(limit),linked.constructors,host_entries);
 rearm::ir::verify(program);
 std::ofstream out(args.at("--output"));if(!out)throw std::runtime_error("cannot open output");rearm::emit::cpp(out,image,program,entry,observed,limit,linked.constructors,jni_onload,jni,libc_memory,libc_heap,instruction_functions);out.close();if(!out)throw std::runtime_error("failed to write generated source");
 std::size_t ops=0;for(const auto& i:program.instructions)ops+=i.ops.size();
 auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"{\"instructions\":"<<program.instructions.size()<<",\"pcode_ops\":"<<ops<<",\"translation_seconds\":"<<seconds<<"}\n";return 0;
 }catch(const std::exception& e){std::cerr<<"ReARM: "<<e.what()<<'\n';return 1;}}

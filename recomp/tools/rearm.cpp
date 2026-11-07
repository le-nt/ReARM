// SPDX-License-Identifier: Apache-2.0
#include "elf/elf_image.hpp"
#include "lift/lifter.hpp"
#include "emit/emitter.hpp"
#include "ir/verify.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
int main(int argc,char** argv){try{
 std::map<std::string,std::string> args;
 for(int i=1;i<argc;i+=2){if(i+1==argc)throw std::runtime_error("option requires value");args[argv[i]]=argv[i+1];}
 for(auto key:{"--input","--sla","--output"})if(!args.contains(key))throw std::runtime_error(std::string("required option: ")+key);
 for(const auto& [key,_]:args)if(key!="--input"&&key!="--sla"&&key!="--output"&&key!="--entry"&&key!="--observe"&&key!="--max-instructions")throw std::runtime_error("unknown option: "+key);
 const auto start=std::chrono::steady_clock::now();
 auto image=rearm::elf::Image::load(args.at("--input"));image.relocate();
 auto address=[&](const std::string& value){if(auto s=image.find_symbol(value))return s->value;std::size_t n=0;auto v=std::stoull(value,&n,0);if(n!=value.size()||v>0xffffffffULL)throw std::runtime_error("invalid guest address: "+value);return static_cast<std::uint32_t>(v);};
 auto entry=args.contains("--entry")?address(args.at("--entry")):image.entry();
 std::vector<std::pair<std::uint32_t,std::uint32_t>> observed;
 if(args.contains("--observe")){auto v=args.at("--observe");auto split=v.find(':');if(split==std::string::npos)throw std::runtime_error("--observe expects symbol:size");auto size=std::stoull(v.substr(split+1),nullptr,0);if(size>1048576)throw std::runtime_error("observation limit exceeded");auto addr=address(v.substr(0,split));if(!image.mapped(addr,size))throw std::runtime_error("observation not mapped");observed.emplace_back(addr,static_cast<std::uint32_t>(size));}
 auto limit=args.contains("--max-instructions")?std::stoull(args.at("--max-instructions")):1000000ULL;
 if(!limit||limit>10000000)throw std::runtime_error("instruction limit must be 1..10000000");
 auto program=rearm::lift::lift(image,args.at("--sla"),entry,static_cast<std::uint32_t>(limit));
 rearm::ir::verify(program);
 std::ofstream out(args.at("--output"));if(!out)throw std::runtime_error("cannot open output");rearm::emit::cpp(out,image,program,entry,observed,limit);out.close();if(!out)throw std::runtime_error("failed to write generated source");
 std::size_t ops=0;for(const auto& i:program.instructions)ops+=i.ops.size();
 auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"{\"instructions\":"<<program.instructions.size()<<",\"pcode_ops\":"<<ops<<",\"translation_seconds\":"<<seconds<<"}\n";return 0;
 }catch(const std::exception& e){std::cerr<<"ReARM: "<<e.what()<<'\n';return 1;}}

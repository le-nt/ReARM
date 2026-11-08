// SPDX-License-Identifier: Apache-2.0
#include "libc_imports32.hpp"
#include "aapcs32.hpp"
namespace rearm::libc32 {
bool dispatchMemory(std::array<std::uint32_t,16>& r,bool& thumb,jni32::GuestMemory& memory){
 auto binding=memoryImport(r[15]&~1u);if(!binding)return false;
 if(thumb||(r[15]&3))throw jni32::Error("libc32 import requires aligned ARM callback target");
 const auto lr=r[14];if(!(lr&1)&&(lr&3))throw jni32::Error("libc32 callback return alignment");
 auto args=aapcs32::marshalWords(r,memory,binding->words);MemoryServices service(memory);std::uint32_t result=0;
 const auto name=binding->name;
 if(name=="memcpy")result=service.copy(args[0],args[1],args[2]);
 else if(name=="memmove")result=service.copy(args[0],args[1],args[2],true);
 else if(name=="memset")result=service.fill(args[0],args[1],args[2]);
 else if(name=="memcmp")result=std::uint32_t(service.compare(args[0],args[1],args[2]));
 else if(name=="strlen")result=service.length(args[0]);
 else if(name=="strnlen")result=service.boundedLength(args[0],args[1]);
 else if(name=="strcmp")result=std::uint32_t(service.compareStrings(args[0],args[1]));
 else if(name=="strncmp")result=std::uint32_t(service.compareStrings(args[0],args[1],args[2]));
 else if(name=="strcpy")result=service.copyString(args[0],args[1]);
 else if(name=="strncpy")result=service.copyString(args[0],args[1],args[2]);
 else throw jni32::UnsupportedFunction("Unknown libc32 import");
 r[0]=result;r[15]=lr&~1u;thumb=(lr&1)!=0;return true;
}
}

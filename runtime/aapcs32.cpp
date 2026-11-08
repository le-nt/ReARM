// SPDX-License-Identifier: Apache-2.0
#include "aapcs32.hpp"
#include <algorithm>
namespace rearm::aapcs32 {
std::vector<std::uint32_t> marshalWords(const std::array<std::uint32_t,16>& r,
    const jni32::GuestMemory& memory,std::uint32_t count,Limits limits) {
 if(!limits.max_words||count>limits.max_words)throw jni32::Error("AAPCS32 argument word limit exceeded");
 if(r[13]&7)throw jni32::Error("AAPCS32 public interface requires 8-byte aligned SP");
 const auto stack_words=count>4?count-4:0;
 if(std::uint64_t(r[13])+std::uint64_t(stack_words)*4>0x100000000ULL)
  throw jni32::MemoryError("AAPCS32 stack argument range wraps");
 std::vector<std::uint32_t> args;args.reserve(count);
 for(std::uint32_t n=0;n<count;++n)args.push_back(n<4?r[n]:memory.readU32(r[13]+(n-4)*4));
 return args;
}
namespace {
std::uint32_t arity(jni32::Function f) {
 using enum jni32::Function;
 switch(f) {
 case GetVersion:case ExceptionOccurred:case ExceptionClear:case ExceptionCheck:
 case DetachCurrentThread:return 1;
 case FindClass:case Throw:case PushLocalFrame:case PopLocalFrame:case NewGlobalRef:
 case DeleteGlobalRef:case DeleteLocalRef:case NewLocalRef:case EnsureLocalCapacity:
 case GetObjectClass:case UnregisterNatives:case GetJavaVM:case GetObjectRefType:return 2;
 case ThrowNew:case IsSameObject:case AttachCurrentThread:case GetEnv:
 case AttachCurrentThreadAsDaemon:return 3;
 case GetMethodID:case GetStaticMethodID:case RegisterNatives:return 4;
 default:throw jni32::UnsupportedFunction("AAPCS32 unsupported JNI callback ID "+std::to_string(static_cast<std::uint32_t>(f)));
 }
}
}
bool dispatchJni(std::array<std::uint32_t,16>& r,bool& thumb,jni32::Host& host,
    jni32::GuestMemory& memory,jni32::ThreadId thread,Limits limits) {
 auto binding=host.bindingForAddress(r[15]&~1u);
 if(!binding)return false;
 if(thumb||(r[15]&3))throw jni32::Error("JNI callback requires aligned ARM target: "+binding->name);
 if(!binding->supported)throw jni32::UnsupportedFunction("Unsupported JNI callback: "+binding->name);
 const auto lr=r[14];
 if(!(lr&1)&&(lr&3))throw jni32::Error("JNI callback return target has invalid ARM alignment");
 auto args=marshalWords(r,memory,arity(binding->function),limits);
 const auto result=host.invoke(binding->function,thread,args);
 r[0]=result;r[15]=lr&~1u;thumb=(lr&1)!=0;return true;
}
}

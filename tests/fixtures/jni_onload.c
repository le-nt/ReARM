// SPDX-License-Identifier: Apache-2.0
// Public freestanding JNI ABI fixture. Tables use JNI 1.6 slot numbers.
typedef unsigned int u32;
typedef int i32;
volatile u32 fixture_output[16];
typedef const u32* Interface;
#ifdef REGISTER_NATIVE
#if defined(__thumb__)
#define OTHER_MODE __attribute__((target("arm")))
#else
#define OTHER_MODE __attribute__((target("thumb")))
#endif
#ifdef REGISTER_STACK
#define NATIVE_SIGNATURE "(IIIIII)I"
#define EXTRA_PARAMETERS ,u32 b,u32 c,u32 d,u32 e,u32 f
#else
#define NATIVE_SIGNATURE "(I)I"
#define EXTRA_PARAMETERS
#endif
static OTHER_MODE __attribute__((noinline)) i32 transform(Interface* env,u32 klass,u32 value EXTRA_PARAMETERS){
 u32 (*version)(Interface*)=(void*)(*env)[4];
 fixture_output[8]=version(env);
 fixture_output[9]=klass!=0;
 fixture_output[10]=value;
 fixture_output[11]=(value^0xc001c0deu)+17u;
#ifdef REGISTER_STACK
 fixture_output[14]=b^c^d^e^f;
 fixture_output[11]+=fixture_output[14];
#endif
 return (i32)fixture_output[11];
}
struct NativeMethod {const char* name;const char* signature;void* entry;};
#endif
__attribute__((visibility("default"))) i32 JNI_OnLoad(Interface* vm,void* reserved){
 Interface* env=0;Interface* returned_vm=0;
 i32 (*get_env)(Interface*,Interface**,u32)=(void*)(*vm)[6];
 fixture_output[0]=(u32)get_env(vm,&env,0x10006);
 if(!env)return -1;
 u32 (*version)(Interface*)=(void*)(*env)[4];
 i32 (*get_vm)(Interface*,Interface**)=(void*)(*env)[219];
 u32 (*find)(Interface*,const char*)=(void*)(*env)[6];
 u32 (*exception)(Interface*)=(void*)(*env)[228];
 void (*clear)(Interface*)=(void*)(*env)[17];
 fixture_output[1]=version(env);
 fixture_output[2]=(u32)get_vm(env,&returned_vm);
 fixture_output[3]=returned_vm==vm;
 fixture_output[4]=find(env,"fixture/Missing");
 fixture_output[5]=exception(env);
 clear(env);
 fixture_output[6]=exception(env);
 fixture_output[7]=reserved==0;
#ifdef STRING_APIS
 u32 (*new_utf)(Interface*,const char*)=(void*)(*env)[167];
 u32 (*new_utf16)(Interface*,const unsigned short*,i32)=(void*)(*env)[163];
 i32 (*units)(Interface*,u32)=(void*)(*env)[164];
 i32 (*utf_bytes)(Interface*,u32)=(void*)(*env)[168];
 void (*region)(Interface*,u32,i32,i32,unsigned short*)=(void*)(*env)[220];
 void (*utf_region)(Interface*,u32,i32,i32,char*)=(void*)(*env)[221];
 u32 text=new_utf(env,"A\xc0\x80\xed\xa0\xbd\xed\xb8\x80");
 fixture_output[8]=(u32)units(env,text);fixture_output[9]=(u32)utf_bytes(env,text);
 unsigned short copied[4]={0xffff,0xffff,0xffff,0xffff};region(env,text,0,4,copied);
 fixture_output[10]=copied[1];fixture_output[11]=((u32)copied[2]<<16)|copied[3];
 u32 second_text=new_utf16(env,copied,4);char encoded[16];for(u32 n=0;n<16;++n)encoded[n]=(char)0xa5;
 utf_region(env,second_text,0,4,encoded);u32 hash=0;for(u32 n=0;n<9;++n)hash=hash*33u+(unsigned char)encoded[n];
 fixture_output[12]=hash;fixture_output[13]=(unsigned char)encoded[9];
 region(env,text,-1,1,copied);fixture_output[14]=exception(env);clear(env);
 fixture_output[15]=(u32)units(env,new_utf16(env,0,0));
#endif
#ifdef REGISTER_NATIVE
 u32 klass=find(env,"fixture/Bridge");
 if(!klass)return -1;
 u32 (*method)(Interface*,u32,const char*,const char*)=(void*)(*env)[113];
 fixture_output[12]=method(env,klass,"transform",NATIVE_SIGNATURE)!=0;
 const struct NativeMethod methods[]={{"transform",NATIVE_SIGNATURE,(void*)transform}};
 i32 (*register_natives)(Interface*,u32,const struct NativeMethod*,i32)=(void*)(*env)[215];
 fixture_output[13]=(u32)register_natives(env,klass,methods,1);
#endif
 return (i32)version(env);
}

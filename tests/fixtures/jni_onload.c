// SPDX-License-Identifier: Apache-2.0
// Public freestanding JNI ABI fixture. Tables use JNI 1.6 slot numbers.
typedef unsigned int u32;
typedef int i32;
volatile u32 fixture_output[16];
typedef const u32* Interface;
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
 return (i32)version(env);
}

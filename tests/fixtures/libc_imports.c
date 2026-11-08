// SPDX-License-Identifier: Apache-2.0
typedef unsigned int u32;
extern void* memcpy(void*,const void*,u32);
extern void* memmove(void*,const void*,u32);
extern void* memset(void*,int,u32);
extern int memcmp(const void*,const void*,u32);
extern u32 strlen(const char*);
extern u32 strnlen(const char*,u32);
extern int strcmp(const char*,const char*);
extern int strncmp(const char*,const char*,u32);
extern char* strcpy(char*,const char*);
extern char* strncpy(char*,const char*,u32);
volatile u32 fixture_output[16];
static char first[96],second[96];
__attribute__((visibility("default"))) u32 fixture_entry(u32 seed){
 for(u32 n=0;n<16;++n)first[n]=(char)(1+((seed>>(n%4)*8)+n)%254);
 first[16]=0;
 fixture_output[0]=memset(second,0x5a,96)==second;
 fixture_output[1]=memcpy(second,first,16)==second;
 fixture_output[2]=memmove(second+3,second,13)==second+3;
 second[16]=0;
 fixture_output[3]=(u32)memcmp(first,second,16);
 fixture_output[4]=strlen(second);
 fixture_output[5]=strnlen(second,7);
 fixture_output[6]=(u32)strcmp(first,second);
 fixture_output[7]=(u32)strncmp(first,second,3);
 fixture_output[8]=strcpy(first,second)==first;
 fixture_output[9]=strncpy(first+32,"abc",8)==first+32;
 u32 hash=seed;for(u32 n=0;n<40;++n)hash=(hash*33u)^(unsigned char)first[n];
 fixture_output[10]=hash;
 fixture_output[11]=(u32)strcmp(first,second);
 fixture_output[12]=strnlen(first+32,8);
 fixture_output[13]=memcmp(first+35,"\0\0\0\0\0",5)==0;
 fixture_output[14]=memcmp(first,second,0)==0;
 fixture_output[15]=seed;
 return hash;
}

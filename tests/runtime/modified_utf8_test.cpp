// SPDX-License-Identifier: Apache-2.0
#include "runtime/modified_utf8.hpp"
#include <iostream>
using namespace rearm::jni32;
void check(bool b){if(!b)throw std::runtime_error("check failed");}
template<class F>void reject(F f){try{f();}catch(const Error&){return;}throw std::runtime_error("missing encoding error");}
int main(){unsigned passed=0,failed=0;auto test=[&](const char* name,auto f){try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}};
 test("empty and ASCII",[]{check(encodeModifiedUtf8(u"hello")=="hello"&&decodeModifiedUtf8("hello")==u"hello");check(encodeModifiedUtf8(u"").empty()&&decodeModifiedUtf8("").empty());});
 test("modified NUL and code-unit boundaries",[]{
  std::u16string units={0,0x7f,0x80,0x7ff,0x800,0xffff};
  std::string bytes="\xc0\x80\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf";
  check(encodeModifiedUtf8(units)==bytes&&decodeModifiedUtf8(bytes)==units);
 });
 test("supplementary characters use six bytes",[]{std::u16string units={0xd83d,0xde00};std::string bytes="\xed\xa0\xbd\xed\xb8\x80";check(encodeModifiedUtf8(units)==bytes&&decodeModifiedUtf8(bytes)==units);});
 test("unpaired surrogate code units are preserved",[]{std::u16string units={0xd800,'x',0xdfff};std::string bytes="\xed\xa0\x80x\xed\xbf\xbf";check(encodeModifiedUtf8(units)==bytes&&decodeModifiedUtf8(bytes)==units);});
 test("malformed encodings rejected",[]{for(auto bytes:{std::string(1,'\0'),std::string("\x80"),std::string("\xc0"),std::string("\xc1\x81"),std::string("\xe0\x80\x80"),std::string("\xc2\x20"),std::string("\xe1\x80"),std::string("\xf0\x9f\x98\x80"),std::string("\xff")})reject([&]{decodeModifiedUtf8(bytes);});});
 test("bounds reject instead of truncating",[]{reject([]{encodeModifiedUtf8(u"ab",1);});reject([]{decodeModifiedUtf8("ab",1);});reject([]{decodeModifiedUtf8("abcd",1);});reject([]{encodeModifiedUtf8(u"",0);});reject([]{decodeModifiedUtf8("",1048577);});});
 test("1000 seeded UTF-16 round trips",[]{std::uint32_t rng=0x4d555446;for(unsigned n=0;n<1000;++n){std::u16string units;for(unsigned i=0;i<n%32;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;units.push_back(char16_t(rng));}auto bytes=encodeModifiedUtf8(units);check(bytes.find('\0')==bytes.npos&&decodeModifiedUtf8(bytes)==units);}});
 std::cout<<"JNI modified UTF-8 tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

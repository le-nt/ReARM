// SPDX-License-Identifier: Apache-2.0
#include "modified_utf8.hpp"
namespace rearm::jni32 {
namespace {
void limit(std::uint32_t units){if(!units||units>1048576)throw Error("Invalid JNI string code-unit limit");}
}
std::u16string decodeModifiedUtf8(std::string_view bytes,std::uint32_t max_units){
 limit(max_units);if(bytes.size()>std::size_t(max_units)*3)throw Error("JNI modified UTF-8 byte limit exceeded");
 std::u16string result;
 for(std::size_t at=0;at<bytes.size();){
  auto first=std::uint8_t(bytes[at++]);std::uint32_t unit=0;
  auto continuation=[&](){if(at==bytes.size())throw Error("Truncated JNI modified UTF-8");auto b=std::uint8_t(bytes[at++]);if((b&0xc0)!=0x80)throw Error("Invalid JNI modified UTF-8 continuation");return b&0x3f;};
  if(first>=1&&first<=0x7f)unit=first;
  else if((first&0xe0)==0xc0){unit=(first&0x1f)<<6;unit|=continuation();if(unit<0x80&&unit!=0)throw Error("Overlong JNI modified UTF-8");}
  else if((first&0xf0)==0xe0){unit=(first&0x0f)<<12;unit|=continuation()<<6;unit|=continuation();if(unit<0x800)throw Error("Overlong JNI modified UTF-8");}
  else throw Error("Invalid JNI modified UTF-8 leading byte (NUL/four-byte UTF-8 are not content encodings)");
  if(result.size()==max_units)throw Error("JNI string code-unit limit exceeded");
  result.push_back(char16_t(unit));
 }
 return result;
}
std::string encodeModifiedUtf8(std::u16string_view units,std::uint32_t max_units){
 limit(max_units);if(units.size()>max_units)throw Error("JNI string code-unit limit exceeded");
 std::string result;result.reserve(units.size());
 for(auto unit:units){
  if(unit>=1&&unit<=0x7f)result.push_back(char(unit));
  else if(unit<=0x7ff){result.push_back(char(0xc0|(unit>>6)));result.push_back(char(0x80|(unit&0x3f)));}
  else{result.push_back(char(0xe0|(unit>>12)));result.push_back(char(0x80|((unit>>6)&0x3f)));result.push_back(char(0x80|(unit&0x3f)));}
 }
 return result;
}
}

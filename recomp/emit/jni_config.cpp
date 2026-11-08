// SPDX-License-Identifier: Apache-2.0
#include "jni_config.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>
namespace rearm::emit {
namespace {
void validClass(const std::string& s){
 if(s.empty()||s.size()>4096||s.front()=='/'||s.back()=='/'||s.find("//")!=s.npos||
   s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$/")!=s.npos)
  throw std::runtime_error("invalid JNI declaration class: "+s);
}
bool type(const std::string& s,std::size_t& at,bool allow_void){
 if(at==s.size())return false;char c=s[at++];
 if(c=='V')return allow_void;
 if(std::string("ZBCSIJFD").find(c)!=std::string::npos)return true;
 if(c=='['){unsigned count=1;while(at<s.size()&&s[at]=='['){++count;++at;}return count<=255&&type(s,at,false);}
 if(c=='L'){auto end=s.find(';',at);if(end==s.npos)return false;validClass(s.substr(at,end-at));at=end+1;return true;}
 return false;
}
void validateMethod(const JniMethod& m){
 validClass(m.class_name);
 if(m.name.empty()||m.name.size()>4096||m.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$")!=m.name.npos)
  throw std::runtime_error("invalid JNI declaration method name");
 const auto& s=m.signature;std::size_t at=1;
 if(s.size()>4096||s.empty()||s[0]!='(')throw std::runtime_error("invalid JNI method descriptor");
 while(at<s.size()&&s[at]!=')')if(!type(s,at,false))throw std::runtime_error("invalid JNI argument descriptor");
 if(at==s.size()||s[at++]!=')'||!type(s,at,true)||at!=s.size())throw std::runtime_error("invalid JNI result descriptor");
}
}
JniMethod parseJniMethod(const std::string& s,bool invocation){
 if(s.size()>16384)throw std::runtime_error("JNI declaration exceeds limit");
 std::vector<std::string> fields;std::size_t start=0;
 for(;;){auto end=s.find('|',start);fields.push_back(s.substr(start,end==s.npos?s.npos:end-start));if(end==s.npos)break;start=end+1;}
 if(fields.size()!=(invocation?3:4))throw std::runtime_error("JNI method expects CLASS|NAME|DESCRIPTOR"+std::string(invocation?"":"|static/instance"));
 if(!invocation&&fields[3]!="static"&&fields[3]!="instance")throw std::runtime_error("JNI method kind expects static or instance");
 JniMethod result{fields[0],fields[1],fields[2],invocation||fields[3]=="static"};validateMethod(result);return result;
}
void validateJniConfig(const JniConfig& config){
 if(config.classes.size()>1024||config.methods.size()>4096||config.invoke.size()>1)throw std::runtime_error("JNI declaration count limit exceeded");
 std::set<std::string> classes;std::set<std::tuple<std::string,std::string,std::string>> methods;
 for(const auto& c:config.classes){validClass(c);if(!classes.insert(c).second)throw std::runtime_error("duplicate JNI class declaration: "+c);}
 for(const auto& m:config.methods){validateMethod(m);if(!classes.contains(m.class_name))throw std::runtime_error("JNI method class not declared: "+m.class_name);
  if(!methods.emplace(m.class_name,m.name,m.signature).second)throw std::runtime_error("duplicate JNI method declaration");}
 for(const auto& m:config.invoke){validateMethod(m);
  auto found=std::find_if(config.methods.begin(),config.methods.end(),[&](const auto& d){return d.class_name==m.class_name&&d.name==m.name&&d.signature==m.signature;});
  if(found==config.methods.end()||!found->is_static)throw std::runtime_error("JNI invocation requires an explicitly declared static native method");
  // Initial host-to-guest call profile accepts jint words and jint/void returns.
  const auto close=m.signature.find(')');
  if(m.signature.substr(1,close-1).find_first_not_of('I')!=std::string::npos||
    (m.signature.substr(close+1)!="I"&&m.signature.substr(close+1)!="V")||close-1>62)
   throw std::runtime_error("JNI invocation supports at most 62 jint arguments and jint/void results");
 }
}
}

// SPDX-License-Identifier: Apache-2.0
#include "emit/jni_config.hpp"
#include <iostream>
#include <stdexcept>
using namespace rearm::emit;
int main(){unsigned passed=0,failed=0;auto accept=[&](auto f,bool valid){try{f();if(!valid)throw std::logic_error("accepted invalid configuration");++passed;}catch(const std::logic_error& e){++failed;std::cerr<<e.what()<<'\n';}catch(const std::runtime_error& e){if(valid){++failed;std::cerr<<e.what()<<'\n';}else ++passed;}};
 for(auto s:{"x/Y|f|(I)I|static","x/Y$Z|f_1|([I[[Ljava/lang/String;)V|instance","x/Y|f|()J|static"})accept([&]{parseJniMethod(s);},true);
 for(auto s:{"x.Y|f|()V|static","x/Y|f|(V)V|static","x/Y|f|(I)Igarbage|static","x/Y|f|([V)I|static","x/Y|f|(L;)I|static","x/Y|f|()I|typo","x/Y|f|()I","x/Y|f\"bad|()I|static","x//Y|f|()I|static"})accept([&]{parseJniMethod(s);},false);
 JniConfig c{{"x/Y"},{parseJniMethod("x/Y|f|(I)I|static")},{parseJniMethod("x/Y|f|(I)I",true)}};
 accept([&]{validateJniConfig(c);},true);
 accept([&]{auto x=c;x.classes.clear();validateJniConfig(x);},false);
 accept([&]{auto x=c;x.classes.push_back("x/Y");validateJniConfig(x);},false);
 accept([&]{auto x=c;x.methods.push_back(x.methods[0]);validateJniConfig(x);},false);
 accept([&]{auto x=c;x.methods[0].is_static=false;validateJniConfig(x);},false);
 accept([&]{auto x=c;x.invoke[0].name="unknown";validateJniConfig(x);},false);
 for(auto sig:{"(J)I","(F)I","(Ljava/lang/String;)I","(I)J"})accept([&]{auto x=c;x.methods[0].signature=sig;x.invoke[0].signature=sig;validateJniConfig(x);},false);
 accept([&]{auto x=c;x.methods[0].signature="("+std::string(63,'I')+")I";x.invoke[0].signature=x.methods[0].signature;validateJniConfig(x);},false);
 std::cout<<"JNI declaration tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

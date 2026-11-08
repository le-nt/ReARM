// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>
namespace rearm::emit {
struct JniMethod {std::string class_name,name,signature;bool is_static;};
struct JniConfig {
 std::vector<std::string> classes;
 std::vector<JniMethod> methods;
 std::vector<JniMethod> invoke;
};
// CLI declarations use CLASS|NAME|DESCRIPTOR|static/instance; invocations omit kind.
JniMethod parseJniMethod(const std::string&,bool invocation=false);
void validateJniConfig(const JniConfig&);
}

// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "jni32.hpp"
#include <string_view>
namespace rearm::jni32 {
// JNI modified UTF-8 content, excluding the external C-string terminator.
// U+0000 is C0 80; UTF-16 surrogate units remain separate three-byte sequences.
// Malformed/overlong input and four-byte standard UTF-8 sequences are rejected.
std::u16string decodeModifiedUtf8(std::string_view bytes,std::uint32_t max_units=4096);
std::string encodeModifiedUtf8(std::u16string_view units,std::uint32_t max_units=4096);
}

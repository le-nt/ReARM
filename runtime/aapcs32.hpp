// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "jni32.hpp"
#include <array>
namespace rearm::aapcs32 {
struct Limits { std::uint32_t max_words=64; };
// Integer/pointer words only: not a floating, wide, struct or variadic ABI.
std::vector<std::uint32_t> marshalWords(const std::array<std::uint32_t,16>&,
    const jni32::GuestMemory&,std::uint32_t count,Limits limits={});
bool dispatchJni(std::array<std::uint32_t,16>&,bool& thumb,jni32::Host&,
    jni32::GuestMemory&,jni32::ThreadId,Limits limits={});
}

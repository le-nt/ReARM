// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "elf/elf_image.hpp"
#include "lift/lifter.hpp"
#include <ostream>
#include <utility>
namespace rearm::emit {
void cpp(std::ostream&, const elf::Image&, const lift::Program&, std::uint32_t entry,
         const std::vector<std::pair<std::uint32_t,std::uint32_t>>& observed,
         std::uint64_t execution_limit,
         const std::vector<std::uint32_t>& initializers = {}, bool jni_onload = false);
}

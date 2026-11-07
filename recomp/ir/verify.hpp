// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "lift/lifter.hpp"
#include <stdexcept>

namespace rearm::ir {
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Verify the normalized integer p-code subset currently supported by the static
// emitter. Throws a guest-address/op-index diagnostic before output is written.
// This is deliberately not a claim of complete p-code or ARM validation.
void verify(const lift::Program& program);
}  // namespace rearm::ir

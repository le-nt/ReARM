// SPDX-License-Identifier: Apache-2.0
// Execute the same modularized Emscripten artifact consumed by Chromium.
'use strict';
const path = require('node:path');
const createModule = require(path.resolve(process.argv[2]));
createModule({ noInitialRun: true }).then(module => {
  module.callMain([process.argv[3] ?? '0']);
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});

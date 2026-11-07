# Confirmed limitations and blockers

This is an implementation checkpoint. Current test outcomes belong in
`TEST_RESULTS.md`; current tasks and blockers belong in `PROGRESS.md`.

- The early translated ARM ELF → static C++ → executed native/Node/Chromium
  comparison passes the public ARM/Thumb O0/O2 fixtures (16 cases, 48 backend
  comparisons). This does not establish complete ARM/Android compatibility.
- WSL initially denied enumeration under restricted permissions. After permissions
  changed, native Linux SLEIGH/compiler builds, CTest and64 translated reference cases
  pass. Fresh Linux bootstrap and hosted CI remain unverified.
- The loader's passing deterministic tests cover a defined ELF32/core REL subset.
  They do not establish a full Android dynamic linker. An initial eager multi-DSO
  profile and constructor execution now pass real-ELF tests; symbol versions,
  advanced/packed relocations, TLS and broader real-linker/fuzz cases remain.
- Raw SLEIGH capture/discovery and the initial integer emitter execute. Complete normalized IR
  verification beyond the tested integer subset, general indirect-target recovery,
  musttail dispatch, VFP/NEON, guest
  exception handling and concurrency are not yet acceptance-tested.
- Public early fixtures are freestanding ARMv7 ET_EXEC programs. Their success
  will not by itself establish Android shared-library or PIE runtime support.
- Android/Bionic/JNI, filesystem, graphics, audio, input and networking bridges
  are roadmap work. No authentic Roblox initialization or gameplay is claimed.
- No authorized exact Roblox client binaries/assets are present. Client-specific
  milestones remain unavailable until their inventory gates can be satisfied;
  general compiler implementation must continue independently.
- The earlier local Git authentication failure is resolved; never force-push main.
- System package and runner-image versions are recorded prerequisites, not a
  content-addressed OS image. Dependency source revisions/SDK/SLA digests are
  locked separately. No stronger reproducibility claim is made before execution.
- Arbitrary self-modifying or runtime-generated ARM code is outside the initial
  static profile. Unknown required instructions/imports/targets must fail visibly.
- The reconstruction Goal is active again after user-controlled reset/resume.
  Local execution still requires an awake, online computer.

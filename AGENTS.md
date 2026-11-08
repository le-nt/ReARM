# ReARM engineering instructions

- Continue the approved `PLAN.md` R01–R32 roadmap. Prioritize working code and
  executed tests; keep documentation proportional to tested checkpoints.
- Work only on `codex/rearm-reconstruction`. Preserve original M0 commit
  `b55568513f697465c6ad22339d1b40b1aa4e4ac1`; do not force-push or merge main.
- Leave `larl/` as reference material and do not modify production 2x2 or Project
  Pizda. This project reconstructs a recompiler and compatibility runtime.
- Production output must statically execute generated operations. Unicorn,
  QEMU, p-code evaluation and ARM decoding belong in compiler/test processes,
  never the production execution engine.
- Do not hardcode fixture results. Test corrections with reusable semantics and
  retain a minimized regression for every discovered correctness bug.
- Unknown instructions, userops, imports, relocation types and branch targets
  require actionable errors; never silently implement required behavior as a no-op.
- Run applicable tests before committing. Reference execution alone does not
  verify translation. Report native, Node and Chromium results independently.
- Keep exact input hashes, dependency revisions and commands in evidence. Missing
  metrics are unavailable, not zero. A sampled peak RSS is not an exact OS peak.
- Keep proprietary binaries/assets/generated translations, private traces,
  secrets and credentials outside Git and public CI artifacts. Use only public
  fixtures and authorized test environments. Never contact legacy defaults merely
  because reference code contains a URL.
- New independently authored code is Apache-2.0. Preserve imported license terms
  and notices; do not relicense the imported skeleton by assumption.
- Independent components may proceed in parallel with explicit file ownership.
  Avoid concurrent edits to the same files; integrate and retest shared changes.
- Make routine technical decisions independently. If blocked, record the exact
  blocker and continue the next unblocked engineering task rather than retrying
  indefinitely or replacing the approved roadmap.
- The user has standing authorization to continue feasible roadmap work after
  tested checkpoints without waiting for another "continue" prompt. Preserve
  the existing Goal; do not create/reset goals to bypass service usage limits.

## Resume a session

Read `PROGRESS.md`, `TEST_RESULTS.md`, `KNOWN_ISSUES.md` and the active milestone.
Inspect branch/status and preserve existing changes. Verify dependency revisions
and fixture hashes before trusting cached artifacts. Reproduce the last relevant
test, then execute the next concrete action. Update the milestone only when its
acceptance gate has passing evidence.

## Environment notes

Linux/Chromium is the intended baseline (`BUILDING.md`). Hosted clean Linux CI
42368507523 passed at 92bc85b, including bootstrap and native/Node/Chromium tests.
WSL Ubuntu24.04 also runs native builds and tests after permissions changed.
Portable LLVM MinGW and embedded Python in the enclosing workspace enable Windows
tests without global installations. Use separate Linux/Windows dependency build
trees and SDK configurations; bootstrap guards reject cross-host reuse.

This host may inherit an unrelated home-directory Git context. If ordinary Git
tries to enter that directory, explicitly set `GIT_DIR` to this checkout's `.git`
and pass this checkout to `git -C`; do not mutate or delete the unrelated repo.
Schannel may fail without credentials even for public reads; an explicit
`-c http.sslBackend=openssl` enabled verified public fetches. Authenticated writes
can use the authorized GitHub connector when CLI credentials are unavailable.

Relevant checks are `ctest --test-dir build/recomp --output-on-failure
--no-tests=error`, Python reference-harness tests, and
`tools/validate_e2e.py --help`. Use actual local tool paths and record them. Never
claim CI or a backend ran merely because its configuration exists.

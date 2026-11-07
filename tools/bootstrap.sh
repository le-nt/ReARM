#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# Never let an outer checkout's explicit Git directory redirect nested fetches.
unset GIT_DIR GIT_WORK_TREE
python="${PYTHON:-python3}"
jobs="${REARM_JOBS:-4}"
case "$jobs" in ''|*[!0-9]*|0) echo 'REARM_JOBS must be a positive integer' >&2; exit 2;; esac
for tool in git cmake ninja "$python"; do
  command -v "$tool" >/dev/null || { echo "Missing prerequisite: $tool (see BUILDING.md)" >&2; exit 2; }
done
export CC="${CC:-clang-18}"
export CXX="${CXX:-clang++-18}"
command -v "$CC" >/dev/null && command -v "$CXX" >/dev/null || {
  echo 'Clang 18 is required; alternatively set CC and CXX explicitly.' >&2; exit 2;
}

lock_value() {
  "$python" - "$root/dependencies.lock.json" "$1" <<'PY'
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for key in sys.argv[2].split("."):
    value = value[key]
print(value)
PY
}

checkout_locked() {
  local name="$1" destination="$root/third_party/$1" revision repository actual
  revision="$(lock_value "$name.revision")"
  repository="$(lock_value "$name.repository")"
  if [[ -e "$destination" ]]; then
    [[ -d "$destination/.git" ]] || { echo "Refusing non-checkout dependency directory: $destination" >&2; exit 2; }
    actual="$(git -C "$destination" rev-parse HEAD)"
    [[ "$actual" == "$revision" ]] || {
      echo "$name is at $actual, expected $revision. Preserve/move that checkout before retrying." >&2; exit 2;
    }
    [[ -z "$(git -C "$destination" status --porcelain --untracked-files=no)" ]] || {
      echo "Refusing modified dependency checkout: $destination" >&2; exit 2;
    }
  else
    git init -q "$destination"
    git -C "$destination" remote add origin "$repository"
    git -C "$destination" fetch --depth 1 origin "$revision"
    git -C "$destination" checkout -q --detach FETCH_HEAD
  fi
}

checkout_locked sleigh
checkout_locked ghidra
checkout_locked emsdk
"$python" - "$root" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
lock = json.loads((root / "dependencies.lock.json").read_text())
sla = lock["arm_sla"]
actual = hashlib.sha256((root / sla["path"]).read_bytes()).hexdigest()
if actual != sla["sha256"]:
    raise SystemExit(f"ARM SLA digest mismatch: {actual}")
sdk_tags = json.loads((root / "third_party/emsdk/emscripten-releases-tags.json").read_text())
sdk = lock["emsdk"]
if sdk_tags["releases"][sdk["sdk_version"]] != sdk["sdk_build"]:
    raise SystemExit("Emscripten SDK release mapping differs from lock")
PY

cmake -S third_party/sleigh -B third_party/sleigh/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$CXX" \
  -DFETCHCONTENT_SOURCE_DIR_GHIDRASOURCE="$root/third_party/ghidra" \
  -Dsleigh_BUILD_EXTRATOOLS=OFF -Dsleigh_BUILD_SUPPORT=OFF \
  -DCMAKE_SKIP_INSTALL_RULES=ON
cmake --build third_party/sleigh/build --parallel "$jobs" --target sleigh_sla

sdk_version="$(lock_value emsdk.sdk_version)"
"$python" - "$root" <<'PY'
import hashlib, json, pathlib, platform, shutil, sys, urllib.request
root = pathlib.Path(sys.argv[1])
sdk = json.loads((root / "dependencies.lock.json").read_text())["emsdk"]
if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
    raise SystemExit("bootstrap.sh currently supports Linux x86_64 only")
archive = root / "third_party/emsdk/downloads" / (sdk["sdk_build"] + "-wasm-binaries.tar.xz")
archive.parent.mkdir(parents=True, exist_ok=True)
if not archive.exists():
    url = "https://storage.googleapis.com/webassembly/emscripten-releases-builds/linux/" + sdk["sdk_build"] + "/wasm-binaries.tar.xz"
    temporary = archive.with_suffix(".part")
    with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as output:
        shutil.copyfileobj(response, output)
    temporary.replace(archive)
digest = hashlib.sha256()
with archive.open("rb") as source:
    for block in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(block)
if digest.hexdigest() != sdk["sdk_archive_sha256_linux_x86_64"]:
    raise SystemExit("Emscripten SDK archive checksum mismatch: " + str(archive))
PY
EMSDK_KEEP_DOWNLOADS=1 third_party/emsdk/emsdk install "$sdk_version"
third_party/emsdk/emsdk activate "$sdk_version"
"$python" -m venv build/venv
build/venv/bin/python - "$root" <<'PY'
import json, pathlib, subprocess, sys
root = pathlib.Path(sys.argv[1])
packages = json.loads((root / "dependencies.lock.json").read_text())["python_packages"]
subprocess.run([sys.executable, "-m", "pip", "install", "--only-binary=:all:", "--no-deps",
                *(f"{name}=={version}" for name, version in packages.items())], check=True)
subprocess.run([sys.executable, "-m", "pip", "check"], check=True)
PY
if [[ "${REARM_INSTALL_BROWSER:-1}" == 1 ]]; then
  build/venv/bin/python -m playwright install chromium
fi

cmake -S recomp -B build/recomp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"
cmake --build build/recomp --parallel "$jobs"
mkdir -p build/evidence
{
  git rev-parse HEAD
  cmake --version
  ninja --version
  node --version
  "$CXX" --version
  "$python" --version
  third_party/emsdk/upstream/emscripten/emcc --version
  build/venv/bin/python -m pip freeze
} > build/evidence/toolchain.txt
echo 'Bootstrap complete. Run: ctest --test-dir build/recomp --output-on-failure'
echo 'Activate test Python: source build/venv/bin/activate'
echo 'Activate SDK: source third_party/emsdk/emsdk_env.sh'

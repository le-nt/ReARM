# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string] $ToolsRoot,
    [string] $BuildRoot,
    [string] $DependencyRoot,
    [string] $SleighBuildRoot,
    [string] $SdkRoot,
    [ValidateRange(1, 64)] [int] $Jobs = 4,
    [switch] $VerifyOnly,
    [switch] $InstallBrowser
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ToolsRoot) { $ToolsRoot = Join-Path $RepoRoot 'build/windows-tools' }
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build/recomp' }
if (-not $DependencyRoot) { $DependencyRoot = Join-Path $RepoRoot 'third_party' }
if (-not $SleighBuildRoot) { $SleighBuildRoot = Join-Path $DependencyRoot 'sleigh/build' }
if (-not $SdkRoot) { $SdkRoot = Join-Path $DependencyRoot 'emsdk' }
$ToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$DependencyRoot = [IO.Path]::GetFullPath($DependencyRoot)
$SleighBuildRoot = [IO.Path]::GetFullPath($SleighBuildRoot)
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$EmConfig = Join-Path $SdkRoot '.emscripten'
$LockPath = Join-Path $RepoRoot 'dependencies.lock.json'
$Lock = Get-Content -LiteralPath $LockPath -Raw | ConvertFrom-Json
$DownloadTool = Join-Path $PSScriptRoot 'download.mjs'
foreach ($Tool in @('node', 'git', 'cmake', 'ctest', 'tar.exe')) {
    if (-not (Get-Command $Tool -ErrorAction SilentlyContinue)) { throw "Missing host prerequisite: $Tool" }
}
function Run-Tool([string] $Program, [string[]] $Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $Program $($Arguments -join ' ')" }
}
function Check-Archive($Package) {
    $Destination = Join-Path $ToolsRoot $Package.archive
    if ($VerifyOnly) { Run-Tool node @($DownloadTool, '--verify', $Destination, $Package.sha256) }
    else { Run-Tool node @($DownloadTool, $Package.url, $Destination, $Package.sha256) }
}
function Git-At([string] $Directory, [string[]] $Arguments) {
    $GitDirectory = Join-Path $Directory '.git'
    # Explicit paths prevent discovering an unrelated parent/home repository.
    & git -c http.sslBackend=openssl --git-dir $GitDirectory --work-tree $Directory -C $Directory @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Git failed in $Directory" }
}
function Ensure-Checkout([string] $Name, [string] $Destination) {
    $Package = $Lock.$Name
    if (-not $Destination) { $Destination = Join-Path $DependencyRoot $Name }
    if (-not (Test-Path -LiteralPath $Destination)) {
        if ($VerifyOnly) { throw "Missing pinned checkout: $Destination" }
        New-Item -ItemType Directory -Path $Destination | Out-Null
        Git-At $Destination @('init', '-q')
        Git-At $Destination @('remote', 'add', 'origin', $Package.repository)
        Git-At $Destination @('fetch', '--depth', '1', 'origin', $Package.revision)
        Git-At $Destination @('checkout', '-q', '--detach', 'FETCH_HEAD')
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Destination '.git'))) { throw "Refusing non-checkout dependency directory: $Destination" }
    $Actual = Git-At $Destination @('rev-parse', 'HEAD')
    if ($Actual -ne $Package.revision) { throw "$Name revision mismatch: $Actual; expected $($Package.revision)" }
    $Changes = Git-At $Destination @('status', '--porcelain', '--untracked-files=no')
    if ($Changes) { throw "Refusing modified dependency checkout: $Destination" }
}

# Sharing pinned source checkouts is supported. Native build trees and SDK
# installations must stay separate between Windows and Linux/WSL.
foreach ($Directory in @($SleighBuildRoot, $BuildRoot)) {
    foreach ($SystemFile in @(Get-ChildItem -Path "$Directory/CMakeFiles/*/CMakeSystem.cmake" -ErrorAction SilentlyContinue)) {
        $Text = Get-Content -LiteralPath $SystemFile.FullName -Raw
        if ($Text -match 'set\(CMAKE_HOST_SYSTEM_NAME "([^"]+)"\)' -and $Matches[1] -ne 'Windows') {
            throw "Refusing $($Matches[1]) CMake build tree $Directory; select separate -SleighBuildRoot and -BuildRoot paths"
        }
    }
}
if (Test-Path -LiteralPath (Join-Path $SdkRoot 'upstream/bin/clang')) {
    throw "Refusing Linux SDK installation $SdkRoot; select a separate -SdkRoot checkout"
}
if ($VerifyOnly -and -not (Test-Path -LiteralPath $EmConfig)) {
    throw "Missing SDK configuration: $EmConfig; -VerifyOnly will not generate one"
}
if (Test-Path -LiteralPath $EmConfig) {
    $ConfigText = Get-Content -LiteralPath $EmConfig -Raw
    if ($ConfigText -match '(?m)^NODE_JS\s*=.*[/\\]bin[/\\]node[''"]?\s*$') {
        throw "Refusing Linux SDK configuration $EmConfig; select a separate -SdkRoot checkout"
    }
}

if (-not $VerifyOnly) { New-Item -ItemType Directory -Path $ToolsRoot -Force | Out-Null }
foreach ($Name in @('llvm_mingw', 'python', 'zlib', 'ghidra_archive', 'get_pip')) {
    Check-Archive $Lock.windows_tools.$Name
}
$LlvmDirectory = Join-Path $ToolsRoot $Lock.windows_tools.llvm_mingw.directory
$PythonDirectory = Join-Path $ToolsRoot $Lock.windows_tools.python.directory
$ZlibSource = Join-Path $ToolsRoot $Lock.windows_tools.zlib.directory
$GhidraSource = Join-Path $DependencyRoot $Lock.windows_tools.ghidra_archive.directory
if (-not $VerifyOnly) {
    New-Item -ItemType Directory -Path $DependencyRoot -Force | Out-Null
    if (-not (Test-Path -LiteralPath $LlvmDirectory)) {
        New-Item -ItemType Directory -Path (Join-Path $ToolsRoot 'llvm') -Force | Out-Null
        Run-Tool tar.exe @('-xf', (Join-Path $ToolsRoot 'llvm-mingw.zip'), '-C', (Join-Path $ToolsRoot 'llvm'))
    }
    if (-not (Test-Path -LiteralPath $PythonDirectory)) {
        New-Item -ItemType Directory -Path $PythonDirectory | Out-Null
        Run-Tool tar.exe @('-xf', (Join-Path $ToolsRoot 'python.zip'), '-C', $PythonDirectory)
    }
    if (-not (Test-Path -LiteralPath $ZlibSource)) {
        Run-Tool tar.exe @('-xf', (Join-Path $ToolsRoot 'zlib.tar.gz'), '-C', $ToolsRoot)
    }
    if (-not (Test-Path -LiteralPath $GhidraSource)) {
        Run-Tool tar.exe @('-xf', (Join-Path $ToolsRoot 'ghidra.zip'), '-C', $DependencyRoot)
    }
    $PthPath = Join-Path $PythonDirectory 'python312._pth'
    $PthText = Get-Content -LiteralPath $PthPath -Raw
    if ($PthText -notmatch '(?m)^import site\s*$') {
        [IO.File]::WriteAllText($PthPath, ($PthText.TrimEnd() + "`nimport site`n"), [Text.UTF8Encoding]::new($false))
    }
}
$Clang = Join-Path $LlvmDirectory 'bin/clang.exe'
$Cxx = Join-Path $LlvmDirectory 'bin/clang++.exe'
$Python = Join-Path $PythonDirectory 'python.exe'
$Ninja = Join-Path $PythonDirectory 'Scripts/ninja.exe'
$ZlibInstall = Join-Path $ToolsRoot 'zlib-install'
$ZlibLibrary = Join-Path $ZlibInstall 'lib/libzlibstatic.a'
$env:PATH = "$(Join-Path $LlvmDirectory 'bin');$(Join-Path $PythonDirectory 'Scripts');$env:PATH"
if (-not $VerifyOnly) {
    Run-Tool $Python @((Join-Path $ToolsRoot 'get-pip.py'), "pip==$($Lock.windows_tools.get_pip.pip_version)")
    $Packages = @($Lock.python_packages.PSObject.Properties | ForEach-Object { "$($_.Name)==$($_.Value)" })
    $Packages += "ninja==$($Lock.windows_tools.ninja_version)"
    Run-Tool $Python (@('-m', 'pip', 'install', '--only-binary=:all:', '--no-deps') + $Packages)
    Run-Tool $Python @('-m', 'pip', 'check')
}
Run-Tool $Clang @('--version')
Run-Tool $Python @('-c', 'import sys; print(sys.version)')
Run-Tool $Ninja @('--version')
Ensure-Checkout 'sleigh'
Ensure-Checkout 'emsdk' $SdkRoot
$SlaPath = Join-Path $RepoRoot $Lock.arm_sla.path
Run-Tool node @($DownloadTool, '--verify', $SlaPath, $Lock.arm_sla.sha256)
if (-not (Test-Path -LiteralPath (Join-Path $GhidraSource 'Ghidra/Features/Decompiler/src/decompile/cpp/sleigh.hh'))) {
    throw "Missing pinned Ghidra source tree: $GhidraSource"
}

$SleighBuild = $SleighBuildRoot
$SdkPython = Join-Path $SdkRoot ($Lock.windows_tools.emsdk_python_directory + '/python.exe')
$env:EM_CONFIG = $EmConfig
if (-not $VerifyOnly) {
    Run-Tool cmake @('-S', $ZlibSource, '-B', (Join-Path $ToolsRoot 'zlib-build'), '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_C_COMPILER=$Clang", '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_INSTALL_PREFIX=$ZlibInstall", '-DZLIB_BUILD_EXAMPLES=OFF')
    Run-Tool cmake @('--build', (Join-Path $ToolsRoot 'zlib-build'), '--parallel', "$Jobs")
    Run-Tool cmake @('--install', (Join-Path $ToolsRoot 'zlib-build'))
    Run-Tool cmake @('-S', (Join-Path $DependencyRoot 'sleigh'), '-B', $SleighBuild, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_CXX_COMPILER=$Cxx", '-DCMAKE_BUILD_TYPE=Release',
        "-DZLIB_LIBRARY=$ZlibLibrary", "-DZLIB_INCLUDE_DIR=$(Join-Path $ZlibInstall 'include')",
        "-DFETCHCONTENT_SOURCE_DIR_GHIDRASOURCE=$GhidraSource", '-Dsleigh_BUILD_EXTRATOOLS=OFF',
        '-Dsleigh_BUILD_SUPPORT=OFF', '-DCMAKE_SKIP_INSTALL_RULES=ON')
    Run-Tool cmake @('--build', $SleighBuild, '--parallel', "$Jobs", '--target', 'sleigh_sla')
    $SdkArchive = Join-Path $SdkRoot ('downloads/' + $Lock.emsdk.sdk_build + '-wasm-binaries.zip')
    $SdkUrl = 'https://storage.googleapis.com/webassembly/emscripten-releases-builds/win/' + $Lock.emsdk.sdk_build + '/wasm-binaries.zip'
    Run-Tool node @($DownloadTool, $SdkUrl, $SdkArchive, $Lock.emsdk.sdk_archive_sha256_windows_x86_64)
    $env:EMSDK_KEEP_DOWNLOADS = '1'
    Run-Tool $Python @((Join-Path $SdkRoot 'emsdk.py'), 'install', $Lock.emsdk.sdk_version)
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($EmConfig)) -Force | Out-Null
    Run-Tool $Python @((Join-Path $SdkRoot 'emsdk.py'), 'activate', $Lock.emsdk.sdk_version)
}
if (-not (Test-Path -LiteralPath $SdkPython)) { throw "Missing SDK full Python: $SdkPython" }
# The embedded test Python's _pth excludes script siblings; emcc requires the
# full SDK Python to import its adjacent emcc module. Keep the two uses distinct.
$env:EMSDK_PYTHON = $SdkPython
$SdkVersion = Run-Tool $SdkPython @((Join-Path $SdkRoot 'upstream/emscripten/emcc.py'), '--version')
$SdkVersion | Write-Output
if (($SdkVersion -join "`n") -notmatch "\) $([regex]::Escape($Lock.emsdk.sdk_version))(\s|$)") {
    throw "Installed Emscripten version does not match locked $($Lock.emsdk.sdk_version)"
}
Run-Tool $Python @('-c', 'import importlib.metadata as m,json,sys; lock=json.load(open(sys.argv[1])); p=dict(lock["python_packages"]); p["ninja"]=lock["windows_tools"]["ninja_version"]; bad=[n for n,v in p.items() if m.version(n)!=v]; assert not bad,bad; expected=tuple(map(int,lock["windows_tools"]["python"]["version"].split("."))); assert sys.version_info[:3]==expected,(sys.version_info,expected); print("locked Python and package versions verified")', $LockPath)
if (-not (Test-Path -LiteralPath $ZlibLibrary)) { throw "Missing static zlib: $ZlibLibrary" }
if (-not (Test-Path -LiteralPath (Join-Path $SleighBuild 'libsla.a'))) { throw 'Missing SLEIGH archive' }
if ($VerifyOnly) {
    Write-Output 'Windows dependency archives, revisions, required files and executable probes verified. No build was run.'
    return
}
Run-Tool cmake @('-S', (Join-Path $RepoRoot 'recomp'), '-B', $BuildRoot, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_C_COMPILER=$Clang", "-DCMAKE_CXX_COMPILER=$Cxx",
    "-DPython3_EXECUTABLE=$Python",
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DZLIB_LIBRARY=$ZlibLibrary",
    "-DZLIB_INCLUDE_DIR=$(Join-Path $ZlibInstall 'include')", "-DMICRODROID_SLEIGH_BUILD=$SleighBuild")
Run-Tool cmake @('--build', $BuildRoot, '--parallel', "$Jobs")
Run-Tool ctest @('--test-dir', $BuildRoot, '--output-on-failure', '--no-tests=error')
if ($InstallBrowser) { Run-Tool $Python @('-m', 'playwright', 'install', 'chromium') }
Write-Output "Build and native tests passed. SDK environment for this process: EMSDK_PYTHON=$SdkPython; EM_CONFIG=$env:EM_CONFIG"

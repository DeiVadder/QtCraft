# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds OpenSSL once for Windows with MSVC and installs it into a cache shared by every preset and
# every checkout (decision E-872), the counterpart of scripts/build-openssl.sh:
# <cache>\<version>-<arch>-<config>, where <cache> is $env:QC_OPENSSL_CACHE or
# %USERPROFILE%\.cache\qtcraft\openssl (next to the SDL3 cache of the configure step), <version>
# the first field of 3rdparty\openssl-version.txt, <arch> the target (x64 or arm64) and <config>
# the version of this script's build options ($ConfigVersion below; raised whenever the options
# change, so an entry built with other options is never reused). The source is the release tarball
# of that version, checked against the SHA-256 in the second field of the same file before it is
# unpacked; the download is kept in <cache>\src.
#
# Built are the shared libraries only, Release: bin\libcrypto-3-<arch>.dll and
# bin\libssl-3-<arch>.dll (the names Qt's OpenSSL backend loads) with their PDB files, the import
# libraries lib\libcrypto.lib and lib\libssl.lib, the headers and LICENSE.txt. Options as on macOS
# and Linux (no programs, tests, docs, engines, loadable modules or legacy provider); x64 with the
# assembler code through NASM (AES-NI, SHA, ...), arm64 without assembler. The prefix the libraries
# know is C:\Windows\qtcraft-openssl, a folder only administrators can create: OPENSSLDIR and the
# folders of loadable modules and engines (MODULESDIR, ENGINESDIR) all lie below it, so no
# openssl.cnf and no module a user puts on the disk changes the game's TLS (the game also
# initialises OpenSSL without reading any configuration; QcTlsBackend). The build installs there
# only through DESTDIR into its scratch folder and moves that tree into the cache entry; the script
# fails when a library still names the scratch folder.
# The compiler is MSVC for the target: the environment of an already imported vcvarsall for that
# target, otherwise vcvarsall itself (found with vswhere; arm64_amd64 on an ARM64 machine, a cross
# build). Perl must be a Windows Perl (Strawberry Perl, $^O MSWin32; the MSYS perl of Git Bash
# builds wrong paths) and, for x64, NASM must be on PATH; jom (C:\Qt\Tools\jom) builds in parallel,
# otherwise nmake. A finished entry has the file .complete and is never built again. The configure
# step of QtCraft runs this script itself when the entry is missing (cmake/QcTls.cmake).
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\build-openssl.ps1
#            [-Arch x64|arm64] [-PrintPrefix]
#   -PrintPrefix  only print the cache directory of this version and target and exit
# Environment: QC_OPENSSL_CACHE (cache root), QC_BUILD_JOBS (parallel jobs; otherwise all cores).
# Exit code 0: the prefix exists (printed last on stdout), 1: the download or build failed,
# 2: wrong use.

param(
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",
    [switch]$PrintPrefix,
    [string]$VcVars = ""
)

# Raise this when the Configure options below change: the cache key then names a new entry.
$ConfigVersion = "w2"
# The prefix compiled into the libraries (OPENSSLDIR, MODULESDIR, ENGINESDIR below it).
$RuntimePrefix = "C:\Windows\qtcraft-openssl"

$ErrorActionPreference = "Continue"
$ProjectDir = Split-Path (Split-Path $PSScriptRoot)

function Fail([int]$code, [string]$message) {
    [Console]::Error.WriteLine("error: $message")
    exit $code
}

function Say([string]$message) {
    [Console]::Error.WriteLine("build-openssl: $message")
}

$pin = (Get-Content -TotalCount 1 (Join-Path $ProjectDir "3rdparty\openssl-version.txt")) -split '\s+'
$Version = $pin[0]
$Sha256 = "$($pin[1])".ToLowerInvariant()
if ($Version -notmatch '^[0-9.]+$') { Fail 2 "3rdparty\openssl-version.txt names no version" }
if ($Sha256.Length -ne 64) { Fail 2 "3rdparty\openssl-version.txt has no SHA-256" }

$Key = "$Version-$Arch-$ConfigVersion"
$CacheRoot = $env:QC_OPENSSL_CACHE
if (-not $CacheRoot) { $CacheRoot = Join-Path $env:USERPROFILE ".cache\qtcraft\openssl" }
$Prefix = Join-Path $CacheRoot $Key
if ($PrintPrefix -or (Test-Path (Join-Path $Prefix ".complete"))) {
    Write-Output $Prefix
    exit 0
}

# One build per entry at a time: the lock is a file held open exclusively, which Windows releases
# when the process ends, however it ends. Waiting runs (other presets, checkouts) wait up to an hour.
New-Item -ItemType Directory -Force (Join-Path $CacheRoot "src") | Out-Null
$lockFile = Join-Path $CacheRoot ".lock-$Key"
$deadline = (Get-Date).AddHours(1)
$lock = $null
while (-not $lock) {
    try {
        $lock = [System.IO.File]::Open($lockFile, "OpenOrCreate", "ReadWrite", "None")
    } catch [System.IO.IOException] {
        # Held by another process; any other error (rights, path) ends the script at once.
        if ((Get-Date) -gt $deadline) { Fail 1 "another build holds $lockFile for more than an hour" }
        Start-Sleep -Seconds 5
    }
}
if (Test-Path (Join-Path $Prefix ".complete")) {
    Write-Output $Prefix
    exit 0
}
# Leftovers of builds that were killed.
Get-ChildItem -Directory -ErrorAction SilentlyContinue $CacheRoot -Filter ".build-$Key.*" | Remove-Item -Recurse -Force
$Work = Join-Path $CacheRoot ".build-$Key.$PID"
New-Item -ItemType Directory -Force $Work | Out-Null

# --- The source: the release tarball, kept once it matches the pinned SHA-256. It is shared by
# the entries of one version (other build options), which hold other locks: the download goes to a
# folder of this process and is renamed in one step; a tarball another build put there meanwhile
# counts when its checksum matches. ---
$System32 = Join-Path $env:SystemRoot "System32"
$Tarball = Join-Path $CacheRoot "src\openssl-$Version.tar.gz"
$Url = "https://github.com/openssl/openssl/releases/download/openssl-$Version/openssl-$Version.tar.gz"
function Get-Sha256([string]$file) { (Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant() }
if (-not (Test-Path $Tarball) -or ((Get-Sha256 $Tarball) -ne $Sha256)) {
    Say "downloading $Url"
    $download = Join-Path $Work "download.tar.gz"
    & (Join-Path $System32 "curl.exe") -fsSL --proto =https --proto-redir =https --retry 3 -o $download $Url
    if ($LASTEXITCODE -ne 0) { Fail 1 "cannot download $Url" }
    $found = Get-Sha256 $download
    if ($found -ne $Sha256) {
        Fail 1 "openssl-$Version.tar.gz has SHA-256 $found, 3rdparty\openssl-version.txt wants $Sha256"
    }
    try {
        Move-Item -Force -ErrorAction Stop $download $Tarball
    } catch {
        if (-not (Test-Path $Tarball) -or ((Get-Sha256 $Tarball) -ne $Sha256)) { Fail 1 "cannot move the download to $Tarball ($_)" }
    }
}

# --- The compiler: MSVC for the target, from vcvarsall unless already imported. ---
$vsTarget = @{ x64 = "x64"; arm64 = "arm64" }[$Arch]
if ($env:VSCMD_ARG_TGT_ARCH -ne $vsTarget -or -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $hostArm = (Get-CimInstance Win32_Processor | Select-Object -First 1).Architecture -eq 12
    $hostArch = @{ $true = "arm64"; $false = "amd64" }[$hostArm]
    $targetArch = @{ x64 = "amd64"; arm64 = "arm64" }[$Arch]
    $vcArgument = @{ $true = $targetArch; $false = "${hostArch}_$targetArch" }[$hostArch -eq $targetArch]
    if (-not $VcVars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vswhere)) { Fail 1 "vswhere.exe not found (Visual Studio 2022 or its Build Tools)" }
        $vsRoot = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
        $VcVars = Join-Path "$vsRoot" "VC\Auxiliary\Build\vcvarsall.bat"
    }
    if (-not (Test-Path $VcVars)) { Fail 1 "vcvarsall.bat not found ($VcVars)" }
    $lines = cmd /c "`"$VcVars`" $vcArgument >nul && set"
    if ($LASTEXITCODE -ne 0) { Fail 1 "vcvarsall $vcArgument failed" }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}
if (-not (Get-Command perl.exe -ErrorAction SilentlyContinue)) { Fail 1 "perl not found (Strawberry Perl on PATH)" }
$perlSystem = "$(& perl.exe -e 'print $^O')".Trim()
if ($perlSystem -ne "MSWin32") { Fail 1 "the perl on PATH is built for '$perlSystem'; OpenSSL needs a Windows Perl (Strawberry Perl, MSWin32)" }
$configureTarget = @{ x64 = "VC-WIN64A"; arm64 = "VC-WIN64-ARM" }[$Arch]
$options = @("shared", "no-tests", "no-docs", "no-apps", "no-engine", "no-module", "no-legacy", "no-ssl3", "no-comp",
    "--prefix=$RuntimePrefix", "--openssldir=$RuntimePrefix", "--libdir=lib")
if ($Arch -eq "arm64") {
    $options += "no-asm"
} elseif (-not (Get-Command nasm.exe -ErrorAction SilentlyContinue)) {
    Fail 1 "nasm not found on PATH (Strawberry Perl brings one in C:\Strawberry\c\bin)"
}
$jom = Get-Command jom.exe -ErrorAction SilentlyContinue
if (-not $jom -and (Test-Path "C:\Qt\Tools\jom\jom.exe")) { $jom = Get-Command "C:\Qt\Tools\jom\jom.exe" }
$jobs = $env:QC_BUILD_JOBS
if (-not $jobs) { $jobs = $env:NUMBER_OF_PROCESSORS }

# --- Build and install into the scratch folder. ---
$sourceDir = Join-Path $Work "source"
# DESTDIR puts the tree of the prefix (without its drive) below the scratch folder.
$destDir = Join-Path $Work "destdir"
$install = Join-Path $destDir $RuntimePrefix.Substring(2)
New-Item -ItemType Directory -Force $sourceDir | Out-Null
& (Join-Path $System32 "tar.exe") -xzf $Tarball -C $sourceDir --strip-components 1
if ($LASTEXITCODE -ne 0) { Fail 1 "cannot unpack $Tarball" }
$log = Join-Path $Work "build.log"
Say "building OpenSSL $Version $Arch ($ConfigVersion, $configureTarget) in $Work"
# Several compilers write the same PDB file under jom: /FS serialises those writes.
$env:CL = "/FS"
Push-Location $sourceDir
$steps = @(
    @("perl.exe", (@("Configure", $configureTarget) + $options)),
    @("make", @("build_libs")),
    @("make", @("DESTDIR=$destDir", "install_dev", "install_runtime_libs"))
)
foreach ($step in $steps) {
    $program = $step[0]
    $arguments = $step[1]
    if ($program -eq "make") {
        if ($jom -and $arguments[0] -eq "build_libs") {
            $program = $jom.Source
            $arguments = @("/J", "$jobs") + $arguments
        } else {
            $program = "nmake.exe"
            $arguments = @("/NOLOGO") + $arguments
        }
    }
    & $program @arguments *>> $log
    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Get-Content -Tail 40 $log | ForEach-Object { [Console]::Error.WriteLine($_) }
        Fail 1 "$program $($arguments -join ' ') failed (log $log)"
    }
}
Pop-Location

# Only the shared libraries: static libraries, pkg-config and CMake files of the build are not used.
Get-ChildItem -ErrorAction SilentlyContinue (Join-Path $install "lib") -Filter "*_static.lib" | Remove-Item -Force
foreach ($folder in @("lib\pkgconfig", "lib\cmake")) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $install $folder)
}
foreach ($file in @("bin\libcrypto-3-$Arch.dll", "bin\libssl-3-$Arch.dll", "lib\libcrypto.lib", "lib\libssl.lib", "include\openssl\opensslv.h")) {
    if (-not (Test-Path (Join-Path $install $file))) { Fail 1 "the build did not install $file (log $log)" }
}
# The folders compiled into libcrypto (OPENSSLDIR, ENGINESDIR, MODULESDIR) lie below the prefix
# only administrators can create, and none lies in the scratch folder. The scratch folder still
# appears in the source file names of error records (__FILE__ of headers) and in the PDB path;
# OpenSSL never opens those.
$crypto = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes((Join-Path $install "bin\libcrypto-3-$Arch.dll")))
foreach ($folder in @("OPENSSLDIR: `"$RuntimePrefix`"", "ENGINESDIR: `"$RuntimePrefix\lib\engines-3`"", "MODULESDIR: `"$RuntimePrefix\lib\ossl-modules`"")) {
    if (-not $crypto.Contains($folder)) { Fail 1 "libcrypto-3-$Arch.dll does not name $folder" }
}
if ([regex]::IsMatch($crypto, [regex]::Escape($Work) + '[^\x00"]*(engines-3|ossl-modules|\.cnf)', "IgnoreCase")) {
    Fail 1 "libcrypto-3-$Arch.dll looks for modules, engines or a configuration in the build folder $Work"
}
Say "OPENSSLDIR, ENGINESDIR and MODULESDIR are below $RuntimePrefix"
# The licence text the package carries (THIRD_PARTY.md): Apache License 2.0.
Copy-Item (Join-Path $sourceDir "LICENSE.txt") (Join-Path $install "LICENSE.txt")
if (-not $?) { Fail 1 "cannot copy the licence text of OpenSSL" }
Set-Content -Path (Join-Path $install ".complete") -Value "$Version $Arch $ConfigVersion"

# Moved into place in one step, so a reader never sees a half-installed entry; a finished entry is
# never replaced.
if (-not (Test-Path (Join-Path $Prefix ".complete"))) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Prefix
    Move-Item $install $Prefix
    if (-not (Test-Path (Join-Path $Prefix ".complete"))) { Fail 1 "$Prefix is incomplete after the move" }
}
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Work
$lock.Close()
Say "done"
Write-Output $Prefix
exit 0

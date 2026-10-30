# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Windows x64 package (stage 74.4, CI mode 39.1c): builds qtcraft and qtcraft-headless, assembles
# a standalone folder with windeployqt, the app-local VC++ runtime, the OpenSSL DLLs, the example
# mods and the licence texts, checks it and packs
#   <name>.zip       (the game, unzip and start qtcraft.exe)
#   <name>-pdb.zip   (PDB files for the minidumps, E-834)
# Two ways to run it:
#   - in the Parallels ARM64 VM (default): a git ref of the Mac repository, cross build ARM64 host
#     -> x64 target (vcvarsall arm64_amd64), preset "release" of CMakeUserPresets.json, into
#     <Root>\dist, <name> = QtCraft-<version>-<shortsha>-Windows-x64
#   - -Ci (the release workflow on GitHub, decision E-934): the checkout this script lies in, the
#     MSVC x64 environment already imported (ilammy/msvc-dev-cmd), Qt from QT_ROOT_DIR
#     (install-qt-action), preset "ci-release", into <checkout>\deploy,
#     <name> = QtCraft-<version>-Windows-x64; SDL3 and OpenSSL come from their default caches
#     below %USERPROFILE%\.cache\qtcraft (restored by actions/cache)
# Without -OpenSourceQt (the Qt kit is not declared as open-source Qt; only the release workflow
# passes it) or when the Qt installation holds commercial licence files (<QtDir>\..\..\Licenses,
# licenseInfo.txt; whoever builds with a commercially licensed Qt keeps getting working packages,
# but only for their own use), the package is for local tests only (decision E-932): both names end in
# -local-only, the package carries NOT-FOR-DISTRIBUTION.txt and a Qt notice without the LGPL
# instead of Qt's LGPL texts.
#
# Checks (any failure makes the script exit with 1):
#   - the build used the Qt kit -QtDir (Qt6_DIR in CMakeCache.txt) and has gamepad support
#     (QC_GAMEPAD=ON with SDL3 linked statically; -NoGamepad accepts a build without it)
#   - every exe/dll in the package imports only DLLs of the package, Windows components in
#     System32 or API sets (dumpbin /dependents, including delay-load imports); a VC++ runtime DLL
#     (vcruntime, msvcp, concrt, vcomp, vccorlib) must come from the package, never from System32;
#     with -Ci only, imports of the Media Foundation DLLs (mfplat, mf, mfreadwrite) that a Windows
#     Server runner lacks are accepted with a log line
#   - all DLLs of the VC++ runtime folder (Microsoft.VC143.CRT or newer), the Qt platform and multimedia plugins, QML modules, example
#     mod, the OpenSSL DLLs (libssl-3-x64.dll, libcrypto-3-x64.dll of the pinned version, E-872)
#     and the licence texts (game, Qt under the LGPL 3 with its notice, SDL3, HIDAPI, SDL libm,
#     OpenSSL, Pixelify Sans) are present; Qt's OpenSSL backend plugin (tls\qopensslbackend.dll) is
#     there; no software OpenGL (opengl32sw.dll); FFmpeg (Qt Multimedia's FFmpeg backend of the
#     installer kits) only with its LGPL 2.1 text and a paragraph in the Qt notice
#   - a copy unpacked from the zip, started with a PATH without Qt: qtcraft-headless --help, a
#     dedicated server (--server --console --port 0 --bind 127.0.0.1, own data directory) that
#     reports "Listening on port", TLS through the bundled OpenSSL of the pinned version and its
#     certificate, whose loaded modules all come from the copy or C:\Windows (the OpenSSL DLLs and
#     Qt's OpenSSL backend from the copy), that a bot of the copy joins over TLS (--join, the
#     certificate pinned), and that stops with /stop
#   - optional (-GuiCheck): qtcraft.exe from the copy renders a world into a screenshot; its loaded
#     modules are checked the same way (a failure there is reported, not fatal: the SSH session of
#     the VM has no interactive desktop)
#
# The source folder <Root>\src is a shallow git checkout (fetched from the Mac repository through
# the Parallels share), so the build stamps the real commit (QcBuildInfo, log headers) and stays
# incremental: the build folder <Root>\src\build\release (gitignored) is kept between runs.
#
# Usage (in the VM, normally through scripts/vm-package-windows.sh on the Mac):
#   powershell -NoProfile -ExecutionPolicy Bypass -File package-windows.ps1 [-Ref dev] [-Jobs 4]
#       [-Root C:\Qt-Projekte\Claude\QtCraft] [-Repo //Mac/Home/Qt-Projekte/Privat/Claude/QtCraft]
#       [-QtDir C:\Qt\<version>\msvc2022_64] [-Server 192.168.0.201:25600] [-NoFetch] [-NoGamepad]
#       [-GuiCheck] [-OpenSourceQt]
# In the release workflow: pwsh -File scripts\windows\package-windows.ps1 -Ci -OpenSourceQt [-Jobs 4]
#   [-Server <text>]
#   -OpenSourceQt  declares -QtDir an open-source Qt (packages for others, E-932)
#   -Server  the address of the playtest server, written into README-Windows.txt
# One run at a time: <Root>\package.lock is held open while the script runs (-Ci: <checkout>\build).
#
# OpenSSL (decision E-872) comes from the cache entry scripts\windows\build-openssl.ps1 builds into
# <Root>\openssl during the configure step (once per version); windeployqt deploys only Qt's TLS
# plugins, so its DLLs are copied next to the executables here, their PDB files into the PDB zip.

param(
    [string]$Ref = "dev",
    [int]$Jobs = 4,
    [string]$Root = "C:\Qt-Projekte\Claude\QtCraft",
    [string]$Repo = "//Mac/Home/Qt-Projekte/Privat/Claude/QtCraft",
    [string]$QtDir = "",
    [string]$VcVars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
    [string]$Server = "192.168.0.201:25600",
    [switch]$NoFetch,
    [switch]$NoGamepad,
    [switch]$GuiCheck,
    [switch]$OpenSourceQt,
    [switch]$Ci
)

$ErrorActionPreference = "Continue"
$Preset = "release"
$Source = Join-Path $Root "src"
$Dist = Join-Path $Root "dist"
if ($Ci) {
    # The checkout this script belongs to; Qt from install-qt-action unless -QtDir names one.
    $Preset = "ci-release"
    $Source = Split-Path (Split-Path $PSScriptRoot)
    $Root = Join-Path $Source "build"
    $Dist = Join-Path $Source "deploy"
    if (-not $PSBoundParameters.ContainsKey("QtDir")) { $QtDir = $env:QT_ROOT_DIR }
    # The VM's default is the LAN address of the playtest server at home; a public package names none.
    if (-not $PSBoundParameters.ContainsKey("Server") -or -not $Server) { $Server = "(die Adresse, die die Spielleitung nennt / the address your host names)" }
}
if (-not $Ci -and -not $QtDir) {
    # Default: the newest Qt 6.8 kit for MSVC x64 below C:\Qt (any Qt 6.8 release works).
    $QtDir = "C:\Qt\6.8\msvc2022_64"
    $kit = Get-ChildItem -Directory -ErrorAction SilentlyContinue "C:\Qt" | Where-Object { $_.Name -match '^6\.8\.\d+$' } |
        Sort-Object { [version]$_.Name } | Select-Object -Last 1
    if ($kit) { $QtDir = Join-Path $kit.FullName "msvc2022_64" }
}
$BuildDir = Join-Path $Source "build\$Preset"
$Work = Join-Path $Root "package-work"
# Media Foundation DLLs that Qt Multimedia imports; Windows Server lacks them without its feature.
$MediaFoundationDlls = @("mfplat.dll", "mf.dll", "mfreadwrite.dll")
# Windows' own bsdtar writes zip files (-a); a tar of Git for Windows on PATH would not.
$Tar = Join-Path $env:SystemRoot "System32\tar.exe"
$script:Failures = @()

function Abort([string]$message) {
    Write-Output "PACKAGE FAILED: $message"
    exit 1
}

function Problem([string]$message) {
    Write-Output "FAIL $message"
    $script:Failures += $message
}

function Ok([string]$message) {
    Write-Output "ok   $message"
}

# Runs a native program, prints its output (stderr included) and aborts on a non-zero exit code.
function Invoke-Checked([string]$what, [string]$exe, [string[]]$arguments) {
    & $exe @arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) {
        Abort "$what failed (exit code $LASTEXITCODE)"
    }
}

# Copies a file the package needs; a missing source aborts the run.
function Copy-Required([string]$from, [string]$to) {
    try {
        Copy-Item -ErrorAction Stop -Recurse $from $to
    } catch {
        Abort "cannot copy $from to $to ($_)"
    }
}

# A DLL of the VC++ runtime: the package carries its own copy, System32 must not serve it.
function Test-RuntimeDll([string]$name) {
    return ($name -match '^(vcruntime|msvcp|concrt|vcomp|vccorlib)[0-9]') -or ($name -match '^ucrtbased\.dll$')
}

# A DLL in System32 counts only if it is part of Windows (product name with "Windows", also when
# localized: "Betriebssystem Microsoft(R) Windows(R)") and not a VC++ runtime DLL.
function Test-WindowsComponent([string]$path) {
    if (Test-RuntimeDll (Split-Path -Leaf $path)) { return $false }
    $product = (Get-Item $path).VersionInfo.ProductName
    return ("$product" -match 'Windows')
}

# Imports the environment of vcvarsall (cross compiler ARM64 host -> x64 target) into this process.
function Import-VcVars {
    $lines = cmd /c "`"$VcVars`" arm64_amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        Abort "vcvarsall arm64_amd64 failed"
    }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        }
    }
    $env:PATH = "C:\Qt\Tools\CMake\bin;C:\Qt\Tools\Ninja;$env:PATH"
}

# The MSVC environment of the workflow (ilammy/msvc-dev-cmd) must target x64 and name the
# redistributable files (VCToolsRedistDir, for the app-local VC++ runtime).
function Assert-CiEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -ne "x64" -or -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Abort "-Ci needs the MSVC x64 environment (VSCMD_ARG_TGT_ARCH is '$env:VSCMD_ARG_TGT_ARCH')"
    }
    if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir "bin\windeployqt.exe"))) {
        Abort "-Ci: no Qt kit with windeployqt in '$QtDir' (QT_ROOT_DIR of install-qt-action, or -QtDir)"
    }
}

function Update-Source {
    if (-not (Test-Path (Join-Path $Source ".git"))) {
        New-Item -ItemType Directory -Force $Source | Out-Null
        Invoke-Checked "git init" "git" @("-C", $Source, "init", "-q")
    }
    Invoke-Checked "git fetch" "git" @("-C", $Source, "-c", "safe.directory=*", "fetch", "-q", "--depth", "1", $Repo, $Ref)
    # reset --mixed fills the index without touching the files; checkout then rewrites only the
    # files that differ, so unchanged sources keep their time stamps and the build stays incremental.
    Invoke-Checked "git reset" "git" @("-C", $Source, "reset", "-q", "--mixed", "FETCH_HEAD")
    Invoke-Checked "git checkout" "git" @("-C", $Source, "checkout", "-q", "-f", "--", ".")
    # Files the ref no longer has (removed or renamed since the last run); ignored files (build
    # folder, CMakeUserPresets.json) and the SDL submodule folder stay.
    $stale = git -C $Source -c core.quotePath=false ls-files --others --exclude-standard | Where-Object { $_ -notlike "3rdparty/*" }
    foreach ($file in $stale) {
        Write-Output "removing stale source file $file"
        Remove-Item -Force (Join-Path $Source $file)
    }
}

# DLL names a PE file imports (normal and delay-load imports), from dumpbin /dependents.
function Get-Imports([string]$file) {
    $output = & dumpbin /nologo /dependents $file 2>&1 | ForEach-Object { "$_" }
    $output | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] }
}

function Test-Imports([string]$package) {
    $system = Join-Path $env:SystemRoot "System32"
    $binaries = Get-ChildItem -Recurse -File $package -Include *.exe, *.dll
    $bad = 0
    foreach ($binary in $binaries) {
        foreach ($import in (Get-Imports $binary.FullName)) {
            if ($import -match '^(api|ext)-ms-') { continue }
            if (Test-Path (Join-Path $package $import)) { continue }
            $inSystem = Join-Path $system $import
            if ((Test-Path $inSystem) -and (Test-WindowsComponent $inSystem)) { continue }
            if ($Ci -and ($MediaFoundationDlls -contains $import.ToLowerInvariant()) -and -not (Test-Path $inSystem)) {
                # Windows Server without the Media Foundation feature (the CI runner); every desktop
                # Windows 10/11 has these DLLs. Only these imports, only with -Ci.
                Write-Output "note $($binary.FullName.Substring($package.Length + 1)) imports $import, which this Windows (no Media Foundation) lacks: accepted in -Ci mode only"
                continue
            }
            Problem "$($binary.FullName.Substring($package.Length + 1)) imports $import (neither in the package nor a Windows component in System32)"
            $bad++
        }
    }
    if ($bad -eq 0) {
        Ok "dumpbin: $($binaries.Count) exe/dll files import only DLLs of the package, Windows components or API sets"
    }
}

# The modules a running process loaded come from the copy or from Windows.
function Test-LoadedModules([System.Diagnostics.Process]$process, [string]$copy, [string]$name) {
    try {
        $process.Refresh()
        $modules = @($process.Modules | ForEach-Object { $_.FileName })
    } catch {
        Problem "${name}: cannot read the loaded modules ($_)"
        return
    }
    $outside = $modules | Where-Object { -not ($_.StartsWith($copy, "OrdinalIgnoreCase") -or
            ($_.StartsWith($env:SystemRoot, "OrdinalIgnoreCase") -and -not (Test-RuntimeDll (Split-Path -Leaf $_)))) }
    if ($modules.Count -eq 0) {
        Problem "${name}: no module list"
    } elseif ($outside) {
        Problem "${name}: modules loaded from outside the copy: $($outside -join ', ')"
    } else {
        Ok "${name}: $($modules.Count) loaded modules, all from the copy or Windows (VC++ runtime from the copy)"
    }
}

# The dedicated server speaks TLS through the OpenSSL of the copy: its log names the certificate it
# made, and both DLLs and Qt's OpenSSL backend are loaded from the copy.
function Test-Tls([System.Diagnostics.Process]$process, [string]$copy, [string]$log) {
    $text = Get-Content -Raw $log
    if ($text -match "Certificate fingerprint \(SHA-256\): ([0-9A-F ]+)") { Ok "dedicated server: certificate $($Matches[1].Trim())" } else { Problem "dedicated server: no certificate fingerprint in $log" }
    try {
        $process.Refresh()
        $modules = @($process.Modules | ForEach-Object { $_.FileName })
    } catch {
        $modules = @()
    }
    foreach ($dll in $opensslDlls + @("tls\qopensslbackend.dll")) {
        $expected = Join-Path $copy $dll
        if ($modules -contains $expected) { Ok "dedicated server loaded $dll from the copy" } else { Problem "dedicated server did not load $expected" }
    }
}

# A bot of the copy joins the server of the copy over TLS (a full handshake between two Windows
# OpenSSL ends, the certificate pinned in the bot's data directory) and leaves at the end of its
# input.
function Test-Join([string]$copy, [string]$log) {
    $port = (Select-String -Path $log -Pattern 'Listening on port (\d+)' | Select-Object -First 1).Matches[0].Groups[1].Value
    $data = Join-Path $Work "join-data"
    New-Item -ItemType Directory -Force $data | Out-Null
    $bot = Start-Copy (Join-Path $copy "qtcraft-headless.exe") "--join 127.0.0.1:$port --player-name PackageCheck --data-dir `"$data`" --time-limit 60" @{}
    $out = $bot.StandardOutput.ReadToEndAsync()
    $err = $bot.StandardError.ReadToEndAsync()
    $bot.StandardInput.Close()
    if (-not $bot.WaitForExit(90000)) {
        $bot.Kill()
        Problem "join bot did not end within 90 s"
    }
    $err.Result | Set-Content (Join-Path $Work "join-stderr.txt")
    $out.Result | Set-Content (Join-Path $Work "join-stdout.txt")
    if ($bot.ExitCode -eq 0 -and "$($err.Result)" -match "joined '") { Ok "a bot of the copy joined the server over TLS (port $port)" } else { Problem "join bot exit code $($bot.ExitCode), see $Work\join-stderr.txt" }
    if (Test-Path (Join-Path $data "known-servers.json")) { Ok "join bot pinned the certificate (known-servers.json)" } else { Problem "join bot has no known-servers.json" }
}

function Start-Copy([string]$exe, [string]$arguments, [hashtable]$environment) {
    $info = New-Object System.Diagnostics.ProcessStartInfo $exe
    $info.Arguments = $arguments
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = Split-Path $exe
    # A PATH without Qt or the build tools: only the copy and Windows.
    $info.EnvironmentVariables["PATH"] = "$env:SystemRoot\System32;$env:SystemRoot"
    # Only the copy's own plugins and QML modules (a Qt installation may have set these).
    $info.EnvironmentVariables.Remove("QT_PLUGIN_PATH")
    $info.EnvironmentVariables.Remove("QML2_IMPORT_PATH")
    # Qt writes a process without a console window to the debugger output instead of stderr; the
    # checks read stderr (on the CI runners the copies have no console).
    $info.EnvironmentVariables["QT_FORCE_STDERR_LOGGING"] = "1"
    foreach ($key in $environment.Keys) {
        $info.EnvironmentVariables[$key] = $environment[$key]
    }
    [System.Diagnostics.Process]::Start($info)
}

function Wait-ForFile([string]$file, [string]$pattern, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $file) -and (Select-String -Quiet -Path $file -Pattern $pattern)) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Test-Server([string]$copy) {
    $exe = Join-Path $copy "qtcraft-headless.exe"
    $help = & cmd /c "set PATH=$env:SystemRoot\System32;$env:SystemRoot&& `"$exe`" --help 2>&1"
    if ($LASTEXITCODE -eq 0 -and ($help -match "--server")) {
        Ok "qtcraft-headless --help"
    } else {
        Problem "qtcraft-headless --help failed (exit code $LASTEXITCODE)"
    }
    $data = Join-Path $Work "server-data"
    New-Item -ItemType Directory -Force $data | Out-Null
    $server = Start-Copy $exe "--server --console --port 0 --bind 127.0.0.1 --generator flat --data-dir `"$data`"" @{}
    $out = $server.StandardOutput.ReadToEndAsync()
    $err = $server.StandardError.ReadToEndAsync()
    $deadline = (Get-Date).AddSeconds(90)
    $log = $null
    while ((Get-Date) -lt $deadline -and -not $log) {
        Start-Sleep -Seconds 1
        $log = Get-ChildItem -ErrorAction SilentlyContinue (Join-Path $data "logs") -Filter *.log | Select-Object -First 1
    }
    if ($log -and (Wait-ForFile $log.FullName "Listening on port" 90)) {
        Ok "dedicated server: $((Select-String -Path $log.FullName -Pattern 'Listening on port' | Select-Object -First 1).Line.Trim())"
        Test-LoadedModules $server $copy "dedicated server"
        Test-Tls $server $copy $log.FullName
        Test-Join $copy $log.FullName
        $head = Get-Content $log.FullName -TotalCount 6
        $head | ForEach-Object { "     $_" }
    } else {
        Problem "dedicated server did not report 'Listening on port' (data: $data)"
    }
    $server.StandardInput.WriteLine("/stop")
    if ($server.WaitForExit(60000)) {
        if ($server.ExitCode -eq 0) { Ok "dedicated server stopped with exit code 0" } else { Problem "dedicated server exit code $($server.ExitCode)" }
    } else {
        Problem "dedicated server did not stop within 60 s"
        $server.Kill()
    }
    $out.Result | Set-Content (Join-Path $Work "server-stdout.txt")
    $err.Result | Set-Content (Join-Path $Work "server-stderr.txt")
    # QcTlsBackend names the library at the start, before the log file exists: standard error.
    if ("$($err.Result)" -match "TLS through (OpenSSL $([regex]::Escape($opensslVersion)) [^\r\n]*)") { Ok "dedicated server: TLS through $($Matches[1])" } else { Problem "dedicated server: no 'TLS through OpenSSL $opensslVersion' on standard error" }
}

function Test-Game([string]$copy) {
    $data = Join-Path $Work "game-data"
    New-Item -ItemType Directory -Force $data | Out-Null
    Set-Content -Path (Join-Path $data "settings.ini") -Value "[audio]`r`nmuted=true"
    $shot = Join-Path $Work "world.png"
    $environment = @{ QTCRAFT_DATA_DIR = $data; QTCRAFT_SCREENSHOT = $shot; QTCRAFT_SCREENSHOT_FRAMES = "90";
        QML_DISK_CACHE_PATH = (Join-Path $Work "qmlcache"); QSG_RHI_DISABLE_DISK_CACHE = "1" }
    $game = Start-Copy (Join-Path $copy "qtcraft.exe") "--play --seed 42 --look 45,-20" $environment
    $out = $game.StandardOutput.ReadToEndAsync()
    $err = $game.StandardError.ReadToEndAsync()
    Start-Sleep -Seconds 15
    if (-not $game.HasExited) {
        Test-LoadedModules $game $copy "game"
    }
    if (-not $game.WaitForExit(150000)) {
        Write-Output "note game run did not end within 165 s, stopped"
        $game.Kill()
    }
    $out.Result | Set-Content (Join-Path $Work "game-stdout.txt")
    $err.Result | Set-Content (Join-Path $Work "game-stderr.txt")
    if ((Test-Path $shot) -and ((Get-Item $shot).Length -gt 50000)) {
        Ok "game screenshot $([int]((Get-Item $shot).Length / 1024)) KiB"
    } else {
        Write-Output "note game run: no screenshot (no interactive desktop in the SSH session?), see $Work\game-*.txt"
    }
}

if ($Ci) {
    Write-Output "== source: this checkout ($Source)"
} else {
    Write-Output "== source $Ref from $Repo"
}
New-Item -ItemType Directory -Force $Root | Out-Null
try {
    $script:Lock = [System.IO.File]::Open((Join-Path $Root "package.lock"), "OpenOrCreate", "ReadWrite", "None")
} catch {
    Abort "another packaging run holds $Root\package.lock"
}
if ($Ci) {
    Assert-CiEnvironment
} else {
    Import-VcVars
}
if (-not $env:VCToolsRedistDir) {
    Abort "VCToolsRedistDir is not set (vcvarsall without the redistributable files?)"
}
if (-not $Ci -and -not $NoFetch) {
    Update-Source
}
if (-not $Ci -and -not (Test-Path (Join-Path $Source "CMakeUserPresets.json"))) {
    Abort "$Source\CMakeUserPresets.json is missing (preset 'release' with the x64 kit, docs/Building.md, section Windows)"
}
$commit = (git -C $Source rev-parse --short=7 HEAD).Trim()
$cmakeLists = Get-Content (Join-Path $Source "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'project\(QtCraft VERSION ([0-9.]+)') {
    Abort "no version in CMakeLists.txt"
}
$version = $Matches[1]
$name = "QtCraft-$version-$commit-Windows-x64"
if ($Ci) {
    # The release names the version only; README-Windows.txt and the log headers name the commit.
    $name = "QtCraft-$version-Windows-x64"
}
# Local tests only unless the Qt kit is declared open source and has no commercial licence files.
$qtRoot = Split-Path (Split-Path $QtDir)
$commercialQt = [bool](Get-ChildItem -ErrorAction SilentlyContinue (Join-Path $qtRoot "Licenses") |
    Where-Object { $_.Name -match "enterprise|commercial" })
$licenseInfo = Join-Path $qtRoot "licenseInfo.txt"
if ((Test-Path $licenseInfo) -and (Select-String -Quiet -Pattern "Enterprise|commercial" $licenseInfo)) { $commercialQt = $true }
if ($commercialQt) { Write-Output "WARNING: $qtRoot holds commercial Qt licence files." }
$localOnly = $commercialQt -or -not $OpenSourceQt
if ($localOnly) {
    $name = "$name-local-only"
    Write-Output "WARNING: local-only package: not for distribution; packages for others come from the release"
    Write-Output "WARNING: workflow with open-source Qt (-OpenSourceQt, decision E-932)."
}
Write-Output "== $name (Qt $QtDir, preset $Preset, $Jobs jobs)"

Write-Output "== configure and build"
Push-Location $Source
if (-not $Ci) {
    $env:QC_SDL_CACHE = (Join-Path $Root "sdl3").Replace("\", "/")
    # OpenSSL's cache entry lives next to the SDL3 one in <Root>, outside the source checkout, so it
    # survives every fetch; the configure step builds it once per version and options
    # (scripts\windows\build-openssl.ps1, with the jobs of this run).
    $env:QC_OPENSSL_CACHE = Join-Path $Root "openssl"
}
$env:QC_BUILD_JOBS = "$Jobs"
$prefix = $QtDir.Replace("\", "/")
Invoke-Checked "configure" "cmake" @("--preset", $Preset, "-DCMAKE_PREFIX_PATH=$prefix")
Invoke-Checked "build" "cmake" @("--build", "--preset", $Preset, "--target", "qtcraft", "qtcraft-headless", "-j", "$Jobs")
Pop-Location
$cache = Get-Content (Join-Path $BuildDir "CMakeCache.txt")
function Get-CacheValue([string]$key) {
    (($cache | Where-Object { $_ -like "${key}:*" }) | Select-Object -First 1) -replace '^[^=]*=', ''
}
$qtUsed = Get-CacheValue "Qt6_DIR"
if (-not $qtUsed.StartsWith($prefix + "/", "OrdinalIgnoreCase")) {
    Abort "the build uses Qt from '$qtUsed', not from $QtDir"
}
$gamepad = Get-CacheValue "QC_GAMEPAD"
if ((Get-CacheValue "QC_TLS") -ne "ON") {
    Abort "the build has no TLS (QC_TLS=$(Get-CacheValue "QC_TLS")): a package must encrypt multiplayer connections"
}
$openssl = Get-CacheValue "QC_OPENSSL_PREFIX"
$opensslDlls = @("libcrypto-3-x64.dll", "libssl-3-x64.dll")
$opensslVersion = ((Get-Content -TotalCount 1 (Join-Path $Source "3rdparty\openssl-version.txt")) -split '\s+')[0]

Write-Output "== assemble"
if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Force $Dist, $Work | Out-Null
# Results of earlier runs (any commit) go; only the new package stays in dist.
Get-ChildItem $Dist -Filter "QtCraft-*-Windows-x64*" | Remove-Item -Recurse -Force
$package = Join-Path $Dist $name
$pdbs = Join-Path $Dist "$name-pdb"
New-Item -ItemType Directory -Force $package, $pdbs | Out-Null
foreach ($exe in @("qtcraft", "qtcraft-headless")) {
    Copy-Required (Join-Path $BuildDir "$exe.exe") $package
    Copy-Required (Join-Path $BuildDir "$exe.pdb") $pdbs
}
# The VC++ runtime goes app-locally next to the exe instead of the redistributable installer
# (no installer, no admin rights; Windows 10/11 bring the Universal CRT). No software OpenGL: the
# game renders with Direct3D 11/12. --force-openssl deploys Qt's OpenSSL backend plugin, which
# windeployqt otherwise skips when it finds no OpenSSL of its own; the DLLs are copied below.
$windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
Invoke-Checked "windeployqt" $windeployqt @("--release", "--force-openssl", "--no-compiler-runtime", "--no-opengl-sw", "--qmldir", (Join-Path $Source "src\app\qml"),
    (Join-Path $package "qtcraft.exe"), (Join-Path $package "qtcraft-headless.exe"))
# qt.conf pins Qt's paths to the folder of the programs: without it Qt also loads plugins from the
# installation prefix built into Qt6Core.dll whenever that folder exists (as on the build machine).
Set-Content -Path (Join-Path $package "qt.conf") -Value "[Paths]`r`nPrefix = .`r`nPlugins = .`r`n" -Encoding ascii
# The runtime folder is named after the toolset (Microsoft.VC143.CRT for Visual Studio 2022,
# a later number for newer ones); the newest one in the redistributable files is taken.
$crt = Get-ChildItem -Directory -ErrorAction SilentlyContinue (Join-Path $env:VCToolsRedistDir "x64") -Filter "Microsoft.VC*.CRT" |
    Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $crt) {
    Abort "no Microsoft.VC*.CRT folder in $(Join-Path $env:VCToolsRedistDir "x64")"
}
$crtDlls = @(Get-ChildItem -File $crt -Filter *.dll | ForEach-Object { $_.Name })
if ($crtDlls.Count -eq 0) {
    Abort "no DLLs in $crt"
}
foreach ($dll in $crtDlls) {
    Copy-Required (Join-Path $crt $dll) (Join-Path $package $dll)
}
foreach ($dll in $opensslDlls) {
    Copy-Required (Join-Path $openssl "bin\$dll") (Join-Path $package $dll)
    Copy-Required (Join-Path $openssl "bin\$([System.IO.Path]::ChangeExtension($dll, "pdb"))") $pdbs
}
# SQL drivers come with QtSql (QtQuick.LocalStorage); the game keeps no data in SQL. Mimer, ODBC
# and PostgreSQL need client libraries outside Qt; SQLite stays (same as scripts/release.sh).
foreach ($driver in @("qsqlmimer.dll", "qsqlodbc.dll", "qsqlpsql.dll")) {
    Remove-Item -ErrorAction SilentlyContinue (Join-Path $package "sqldrivers\$driver")
}
$mods = Join-Path $package "mods"
New-Item -ItemType Directory -Force $mods | Out-Null
foreach ($mod in (Get-ChildItem -Directory (Join-Path $Source "mods"))) {
    if (Test-Path (Join-Path $mod.FullName "pack.json")) {
        Copy-Required $mod.FullName (Join-Path $mods $mod.Name)
    }
}
$licenses = Join-Path $package "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
foreach ($file in @("LICENSE", "THIRD_PARTY.md", "CREDITS.md")) {
    Copy-Required (Join-Path $Source $file) (Join-Path $package $file)
}
Copy-Required (Join-Path $Source "src\app\fonts\OFL.txt") (Join-Path $licenses "PixelifySans-OFL.txt")
Copy-Required (Join-Path $openssl "LICENSE.txt") (Join-Path $licenses "OpenSSL-LICENSE.txt")
$qtVersion = "$(& (Join-Path $QtDir "bin\qtpaths.exe") --query QT_VERSION)".Trim()
if ($qtVersion -notmatch '^6\.') {
    Abort "cannot read the Qt version from $QtDir\bin\qtpaths.exe"
}
$qtSeries = $qtVersion.Substring(0, $qtVersion.LastIndexOf('.'))
# Qt Multimedia's FFmpeg libraries (the FFmpeg backend of the installer and CI kits; a kit built from
# the sources may have only Windows Media) are under the LGPL 2.1 or later: their text and a
# paragraph of the notice, only when they are there. The FFmpeg tag Qt builds from is named in Qt's
# provisioning script, as in scripts/release.sh. $ffmpeg (detection) includes Qt's FFmpeg backend
# plugin; the notice names only the FFmpeg libraries ($ffmpegLibraries, av*/sw*): the plugin is
# Qt's own code under the LGPL 3.
$ffmpeg = @(Get-ChildItem -Recurse -File $package -Include "ffmpegmediaplugin.dll", "av*.dll", "swscale*.dll", "swresample*.dll" | ForEach-Object { $_.Name } | Sort-Object -Unique)
$ffmpegLibraries = @($ffmpeg | Where-Object { $_ -match '^(av|sw)' })
$ffmpegFiles = "The FFmpeg libraries in this package ($($ffmpegLibraries -join ', ')) are"
if ($ffmpegLibraries.Count -eq 0) {
    $ffmpegFiles = "FFmpeg, linked into Qt's plugin multimedia\ffmpegmediaplugin.dll, is"
}
$ffmpegScript = "coin/provisioning/common/windows/install-ffmpeg.ps1 of the repository qt/qt5 at the tag v$qtVersion"
$ffmpegTag = "the FFmpeg tag named in $ffmpegScript"
$ffmpegUrlTag = "<tag>"
if ($qtVersion -eq "6.8.3") {
    $ffmpegTag = "FFmpeg n7.1 (named in $ffmpegScript)"
    $ffmpegUrlTag = "n7.1"
}
if ($ffmpeg.Count -gt 0) {
    Copy-Required (Join-Path $Source "licenses-third-party\LGPL-2.1-or-later.txt") (Join-Path $licenses "FFmpeg-LGPL-2.1.txt")
}
if (-not $localOnly) {
    # Qt is used under the LGPL 3 (THIRD_PARTY.md): its text, the GPL 3 it builds on (the game's own
    # LICENSE), and a notice with the exact source (decision E-932). The texts come from the
    # repository, as in scripts/release.sh: the CI kits of install-qt-action have no Qt sources.
    Copy-Required (Join-Path $Source "licenses-third-party\LGPL-3.0-only.txt") (Join-Path $licenses "Qt-LGPL-3.0.txt")
    Copy-Required (Join-Path $Source "LICENSE") (Join-Path $licenses "Qt-GPL-3.0.txt")
    $qtNotice = @(
        "Qt $qtVersion",
        "",
        "This package contains the libraries, plugins and QML modules of Qt $qtVersion (The Qt Company Ltd.",
        "and other contributors) as separate, unmodified DLL files next to qtcraft.exe and in its plugin and",
        "qml folders. They are used under the GNU Lesser General Public License version 3 (Qt-LGPL-3.0.txt,",
        "which builds on Qt-GPL-3.0.txt); you may replace them with your own build of the same Qt version.",
        "Qt contains code of other projects (for example libpng, libjpeg, PCRE2, HarfBuzz, double-conversion,",
        "TinyCBOR); their licences and notices are listed in the software bills of materials in the folder",
        "Qt-SBOM, where present, and in the Qt documentation (`"Licenses Used in Qt`").",
        ""
    )
    if ($ffmpeg.Count -gt 0) {
        $qtNotice += @(
            "Qt Multimedia plays sound through FFmpeg (Qt's backend plugin multimedia\ffmpegmediaplugin.dll).",
            "$ffmpegFiles the unmodified LGPL build that comes with Qt $qtVersion;",
            "FFmpeg is licensed under the GNU Lesser General Public License version 2.1 or later",
            "(FFmpeg-LGPL-2.1.txt). You may replace them with your own build of the same FFmpeg version.",
            ""
        )
    } else {
        $qtNotice += @("Qt Multimedia is built with the Windows Media backend only (no FFmpeg).", "")
    }
    $qtNotice += @(
        "Corresponding source code:",
        "- Qt ${qtVersion}: https://download.qt.io/archive/qt/$qtSeries/$qtVersion/single/qt-everywhere-src-$qtVersion.tar.xz",
        "  (the same folder also has a .zip and md5sums.txt)."
    )
    if ($ffmpeg.Count -gt 0) {
        $qtNotice += @(
            "- FFmpeg: $ffmpegTag,",
            "  https://github.com/FFmpeg/FFmpeg/archive/refs/tags/$ffmpegUrlTag.tar.gz (also from ffmpeg.org); the build",
            "  options are in that script."
        )
    }
    $qtNotice += @("The QtCraft maintainers also send these sources on request. No software OpenGL is included.")
} else {
    $qtNotice = @(
        "Qt $qtVersion (local tests only: not declared as open-source Qt)",
        "",
        "This package was built for local tests with the Qt kit $QtDir, which is not declared as",
        "open-source Qt. Its Qt DLLs, plugins and QML modules are not passed on under the GNU LGPL by",
        "this package; do not distribute it (NOT-FOR-DISTRIBUTION.txt). Packages for others come from the",
        "release workflow, built against open-source Qt."
    )
    if ($ffmpeg.Count -gt 0) {
        $qtNotice += @(
            "",
            "$ffmpegFiles licensed under the GNU Lesser",
            "General Public License version 2.1 or later (FFmpeg-LGPL-2.1.txt); source: $ffmpegTag."
        )
    }
    [System.IO.File]::WriteAllLines((Join-Path $package "NOT-FOR-DISTRIBUTION.txt"), @(
        "This QtCraft package was built with a Qt that is not declared as open-source Qt ($QtDir), for local tests only.",
        "Do not pass it on: packages for others come from the release workflow with open-source Qt."))
}
[System.IO.File]::WriteAllLines((Join-Path $licenses "Qt-NOTICE.txt"), $qtNotice)
# Qt's software bills of materials for the modules in the package, when the Qt installation has them.
foreach ($module in @("qtbase", "qtdeclarative", "qtmultimedia", "qtimageformats", "qtsvg")) {
    $sbom = Join-Path $QtDir "sbom\$module-$qtVersion.spdx"
    if (Test-Path $sbom) {
        New-Item -ItemType Directory -Force (Join-Path $licenses "Qt-SBOM") | Out-Null
        Copy-Required $sbom (Join-Path $licenses "Qt-SBOM")
    }
}
$sdl = $null
if ($gamepad -eq "ON") {
    # SDL3_DIR is <prefix>/cmake of the SDL3 cache entry the build linked (scripts/build-sdl.sh).
    $sdl = Split-Path (Get-CacheValue "SDL3_DIR")
    Copy-Required (Join-Path $sdl "LICENSE.txt") (Join-Path $licenses "SDL3-LICENSE.txt")
    Copy-Required (Join-Path $sdl "HIDAPI-LICENSE.txt") (Join-Path $licenses "HIDAPI-LICENSE.txt")
    Copy-Required (Join-Path $sdl "LIBM-NOTICE.txt") (Join-Path $licenses "SDL3-LIBM-NOTICE.txt")
}
$readme = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "README-Windows.txt")
$readme = $readme.Replace("@VERSION@", $version).Replace("@COMMIT@", $commit).Replace("@SERVER@", $Server)
$readme = $readme.Replace("`r`n", "`n").Replace("`n", "`r`n")
[System.IO.File]::WriteAllText((Join-Path $package "README-Windows.txt"), $readme, (New-Object System.Text.UTF8Encoding $true))

Write-Output "== checks"
if ($Ci) {
    $missingMf = @($MediaFoundationDlls | Where-Object { -not (Test-Path (Join-Path $env:SystemRoot "System32\$_")) })
    if ($missingMf.Count -gt 0) {
        Write-Output "note Media Foundation is missing on this Windows ($($missingMf -join ', ')): imports of exactly these DLLs are accepted (-Ci)"
    } else {
        Ok "Media Foundation present (mfplat.dll, mf.dll, mfreadwrite.dll)"
    }
}
$required = @("qtcraft.exe", "qtcraft-headless.exe", "qt.conf", "Qt6Core.dll", "Qt6Quick.dll", "Qt6Multimedia.dll",
    "platforms\qwindows.dll", "multimedia\windowsmediaplugin.dll", "qml\QtQuick\Controls", "qml\QtQuick\Layouts",
    "mods\rubies\pack.json", "LICENSE", "THIRD_PARTY.md", "CREDITS.md", "README-Windows.txt", "licenses\PixelifySans-OFL.txt",
    "licenses\Qt-NOTICE.txt", "licenses\OpenSSL-LICENSE.txt",
    "tls\qopensslbackend.dll") + $crtDlls + $opensslDlls
if ($localOnly) {
    $required += @("NOT-FOR-DISTRIBUTION.txt")
} else {
    $required += @("licenses\Qt-LGPL-3.0.txt", "licenses\Qt-GPL-3.0.txt")
}
foreach ($part in $required) {
    if (Test-Path (Join-Path $package $part)) { Ok "contains $part" } else { Problem "missing $part" }
}
foreach ($dll in @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")) {
    if ($crtDlls -notcontains $dll) { Problem "$(Split-Path -Leaf $crt) has no $dll" }
}
# The DLLs are the ones of the pinned version (OpenSSL writes its version into their resources).
foreach ($dll in $opensslDlls) {
    $fileVersion = (Get-Item (Join-Path $package $dll)).VersionInfo.ProductVersion
    if ("$fileVersion" -eq $opensslVersion) { Ok "$dll is OpenSSL $fileVersion" } else { Problem "$dll is OpenSSL '$fileVersion', 3rdparty\openssl-version.txt wants $opensslVersion" }
}
if (Test-Path (Join-Path $package "opengl32sw.dll")) { Problem "the package contains opengl32sw.dll (Mesa, needs its notice)" } else { Ok "no software OpenGL (opengl32sw.dll)" }
# FFmpeg (the installer kits' backend) only with its LGPL 2.1 text and the paragraph of the notice.
if ($ffmpeg.Count -eq 0) {
    Ok "no FFmpeg backend or libraries"
} elseif ((Test-Path (Join-Path $licenses "FFmpeg-LGPL-2.1.txt")) -and (Select-String -Quiet -Path (Join-Path $licenses "Qt-NOTICE.txt") -Pattern "FFmpeg-LGPL-2.1.txt")) {
    Ok "FFmpeg files ($($ffmpeg -join ', ')) with licenses\FFmpeg-LGPL-2.1.txt and the paragraph in Qt-NOTICE.txt"
} else {
    Problem "the package contains FFmpeg files without their notice: $($ffmpeg -join ', ')"
}
if ($gamepad -eq "ON") {
    if (Get-ChildItem -Recurse -File $package -Filter "*SDL*.dll") { Problem "the package contains an SDL library" } else { Ok "SDL3 linked statically from $sdl (no SDL library)" }
    foreach ($licence in @("SDL3-LICENSE.txt", "HIDAPI-LICENSE.txt", "SDL3-LIBM-NOTICE.txt")) {
        if (Test-Path (Join-Path $licenses $licence)) { Ok "contains licenses\$licence" } else { Problem "missing licenses\$licence" }
    }
} elseif ($NoGamepad) {
    Ok "built without gamepad support (QC_GAMEPAD=$gamepad, -NoGamepad)"
} else {
    Problem "built without gamepad support (QC_GAMEPAD=$gamepad); pass -NoGamepad if that is intended"
}
$machine = & dumpbin /nologo /headers (Join-Path $package "qtcraft.exe") | Select-String "machine \("
if ("$machine" -match "x64") { Ok "qtcraft.exe is x64" } else { Problem "qtcraft.exe is not x64 ($machine)" }
Test-Imports $package

Write-Output "== zip"
& $Tar -a -c -f "$package.zip" -C $Dist $name
if ($LASTEXITCODE -ne 0) { Abort "zip failed" }
& $Tar -a -c -f "$pdbs.zip" -C $Dist "$name-pdb"
if ($LASTEXITCODE -ne 0) { Abort "pdb zip failed" }

Write-Output "== start a copy unpacked from the zip"
$copyRoot = Join-Path $Work "copy"
New-Item -ItemType Directory -Force $copyRoot | Out-Null
& $Tar -x -f "$package.zip" -C $copyRoot
if ($LASTEXITCODE -ne 0) { Abort "cannot unpack $package.zip" }
$copy = Join-Path $copyRoot $name
Test-Server $copy
if ($GuiCheck) {
    Test-Game $copy
}

foreach ($zip in @("$package.zip", "$pdbs.zip")) {
    Write-Output ("     {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
}
if ($script:Failures.Count -gt 0) {
    Write-Output "PACKAGE CHECKS FAILED: $($script:Failures.Count) problem(s), logs in $Work"
    exit 1
}
Write-Output "PACKAGE OK $package.zip"
exit 0

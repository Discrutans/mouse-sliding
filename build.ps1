<#
.SYNOPSIS
  Configure and build Mouse Sliding for OBS Studio (Windows x64).
  For end-user install use package-installer.ps1 (creates setup.exe).
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

$cmakeCandidates = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "${env:ProgramFiles}\CMake\bin\cmake.exe",
    "cmake"
)

$cmake = $null
foreach ($candidate in $cmakeCandidates) {
    if ($candidate -eq 'cmake') {
        $cmd = Get-Command cmake -ErrorAction SilentlyContinue
        if ($cmd) { $cmake = $cmd.Source; break }
    } elseif (Test-Path $candidate) {
        $cmake = $candidate
        break
    }
}

if (-not $cmake) {
    throw "CMake not found. Install Visual Studio 2022 with C++ Desktop workload."
}

Write-Host "Using CMake: $cmake"
Write-Host "Configuring..."

& $cmake --preset windows-x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

Write-Host "Building ($Configuration)..."
& $cmake --build --preset windows-x64 --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

$dll = Join-Path $ProjectRoot "build_x64\rundir\$Configuration\mouse-sliding.dll"
if (-not (Test-Path $dll)) {
    $found = Get-ChildItem -Path (Join-Path $ProjectRoot "build_x64") -Recurse -Filter "mouse-sliding.dll" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { $dll = $found.FullName } else { $dll = $null }
}

Write-Host ""
Write-Host "Build succeeded."
if ($dll) {
    Write-Host "Plugin DLL: $dll"
}
Write-Host "To create setup.exe: .\package-installer.ps1"

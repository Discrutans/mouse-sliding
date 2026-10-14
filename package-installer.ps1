<#
.SYNOPSIS
  Build Mouse Sliding, stage ProgramData-style package under dist\, and compile Inno Setup installer.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

$PluginName = 'mouse-sliding'
$Version = '1.0.0'
$DistRoot = Join-Path $ProjectRoot "dist\$PluginName"
$DistBin = Join-Path $DistRoot 'bin\64bit'
$DistData = Join-Path $DistRoot 'data'
$Iss = Join-Path $ProjectRoot 'installer\mouse-sliding.iss'

if (-not $SkipBuild) {
    & (Join-Path $ProjectRoot 'build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed" }
}

$dll = Join-Path $ProjectRoot "build_x64\rundir\$Configuration\$PluginName.dll"
if (-not (Test-Path $dll)) {
    $found = Get-ChildItem -Path (Join-Path $ProjectRoot 'build_x64') -Recurse -Filter "$PluginName.dll" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { $dll = $found.FullName } else { throw "DLL not found. Build the plugin first." }
}

Write-Host "Staging package from: $dll"
if (Test-Path $DistRoot) { Remove-Item $DistRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $DistBin | Out-Null
New-Item -ItemType Directory -Force -Path $DistData | Out-Null
Copy-Item $dll -Destination (Join-Path $DistBin "$PluginName.dll") -Force
Copy-Item (Join-Path $ProjectRoot 'data\*') -Destination $DistData -Recurse -Force

$isccCandidates = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "${env:LocalAppData}\Programs\Inno Setup 6\ISCC.exe",
    'ISCC.exe'
)
$iscc = $null
foreach ($c in $isccCandidates) {
    if ($c -eq 'ISCC.exe') {
        $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($cmd) { $iscc = $cmd.Source; break }
    } elseif (Test-Path $c) {
        $iscc = $c
        break
    }
}

if (-not $iscc) {
    Write-Host "Inno Setup 6 not found. Installing via winget..."
    winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements
    $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (-not (Test-Path $iscc)) {
        throw "ISCC.exe still not found after Inno Setup install."
    }
}

Write-Host "Compiling installer with: $iscc"
& $iscc $Iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile failed" }

$setup = Join-Path $ProjectRoot "dist\$PluginName-windows-x64-v$Version-setup.exe"
if (Test-Path $setup) {
    Write-Host ""
    Write-Host "Installer ready: $setup"
} else {
    Write-Host "Warning: expected setup not found at $setup"
    Get-ChildItem (Join-Path $ProjectRoot 'dist') -Filter '*.exe' | ForEach-Object { Write-Host $_.FullName }
}

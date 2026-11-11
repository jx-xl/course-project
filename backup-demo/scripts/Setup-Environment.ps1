[CmdletBinding()]
param(
    [switch]$InstallMissing,
    [switch]$Build,
    [switch]$RunTests,
    [string]$QtRoot,
    [string]$QtVersion = '6.8.3'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Invoke-Checked([string]$program, [string[]]$arguments) {
    Write-Host "> $program $($arguments -join ' ')" -ForegroundColor DarkGray
    & $program @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $program"
    }
}

function Find-Python {
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($command) {
        & $command.Source --version *> $null
        if ($LASTEXITCODE -eq 0) {
            return $command.Source
        }
    }
    $localPrograms = Join-Path $env:LOCALAPPDATA 'Programs\Python'
    if (Test-Path -LiteralPath $localPrograms) {
        $candidate = Get-ChildItem -LiteralPath $localPrograms -Filter python.exe -File -Recurse |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }
    return $null
}

function Install-QtEnvironment([string]$targetRoot) {
    if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        throw 'WinGet is required for automatic Python installation. Install App Installer or use the manual Qt setup described in README.md.'
    }

    $python = Find-Python
    if (-not $python) {
        Write-Host 'Installing Python 3.12 with WinGet...' -ForegroundColor Cyan
        Invoke-Checked 'winget.exe' @(
            'install', '--id', 'Python.Python.3.12', '--exact', '--silent',
            '--accept-source-agreements', '--accept-package-agreements'
        )
        $python = Find-Python
    }
    if (-not $python) {
        throw 'Python was installed but could not be found. Open a new PowerShell window and run this script again.'
    }

    Write-Host 'Installing the Qt download helper...' -ForegroundColor Cyan
    Invoke-Checked $python @('-m', 'pip', 'install', '--user', '--upgrade', 'aqtinstall')

    $qtDirectory = Join-Path $targetRoot "$QtVersion\mingw_64"
    if (-not (Test-Path -LiteralPath (Join-Path $qtDirectory 'bin\Qt6Core.dll'))) {
        Write-Host "Installing Qt $QtVersion MinGW 64-bit..." -ForegroundColor Cyan
        Invoke-Checked $python @(
            '-m', 'aqt', 'install-qt', '--outputdir', $targetRoot,
            'windows', 'desktop', $QtVersion, 'win64_mingw'
        )
    }

    $toolsRoot = Join-Path $targetRoot 'Tools'
    if (-not (Get-ChildItem -LiteralPath $toolsRoot -Filter g++.exe -File -Recurse -ErrorAction SilentlyContinue)) {
        Invoke-Checked $python @(
            '-m', 'aqt', 'install-tool', '--outputdir', $targetRoot,
            'windows', 'desktop', 'tools_mingw1310', 'qt.tools.win64_mingw1310'
        )
    }
    if (-not (Test-Path -LiteralPath (Join-Path $toolsRoot 'CMake_64\bin\cmake.exe'))) {
        Invoke-Checked $python @(
            '-m', 'aqt', 'install-tool', '--outputdir', $targetRoot,
            'windows', 'desktop', 'tools_cmake'
        )
    }
    if (-not (Test-Path -LiteralPath (Join-Path $toolsRoot 'Ninja\ninja.exe'))) {
        Invoke-Checked $python @(
            '-m', 'aqt', 'install-tool', '--outputdir', $targetRoot,
            'windows', 'desktop', 'tools_ninja'
        )
    }
    if (-not (Get-ChildItem -LiteralPath $toolsRoot -Filter qtcreator.exe -File -Recurse -ErrorAction SilentlyContinue)) {
        Invoke-Checked $python @(
            '-m', 'aqt', 'install-tool', '--outputdir', $targetRoot,
            'windows', 'desktop', 'tools_qtcreator'
        )
    }
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $candidateRoots = @(
        'C:\Qt',
        (Join-Path $env:USERPROFILE 'Qt')
    ) | Select-Object -Unique
    $QtRoot = $candidateRoots |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ "$QtVersion\mingw_64") } |
        Select-Object -First 1
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    if (-not $InstallMissing) {
        throw 'Qt was not found. Re-run with -InstallMissing, or install Qt manually and pass -QtRoot C:\path\to\Qt.'
    }
    $QtRoot = Join-Path $env:USERPROFILE 'Qt'
}

$QtRoot = [System.IO.Path]::GetFullPath($QtRoot)
if ($InstallMissing) {
    Install-QtEnvironment $QtRoot
}

$qtDirectory = Join-Path $QtRoot "$QtVersion\mingw_64"
$toolsRoot = Join-Path $QtRoot 'Tools'
$mingwDirectory = Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter 'mingw*_64' -ErrorAction SilentlyContinue |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\g++.exe') } |
    Sort-Object Name -Descending |
    Select-Object -First 1
$cmakeExe = Join-Path $toolsRoot 'CMake_64\bin\cmake.exe'
$ninjaExe = Join-Path $toolsRoot 'Ninja\ninja.exe'

$missing = @()
if (-not (Test-Path -LiteralPath (Join-Path $qtDirectory 'bin\Qt6Core.dll'))) { $missing += 'Qt 6 MinGW kit' }
if (-not $mingwDirectory) { $missing += 'MinGW-w64 compiler' }
if (-not (Test-Path -LiteralPath $cmakeExe)) { $missing += 'CMake' }
if (-not (Test-Path -LiteralPath $ninjaExe)) { $missing += 'Ninja' }
if ($missing.Count -gt 0) {
    throw "Incomplete environment under $QtRoot. Missing: $($missing -join ', ')."
}

$mingwBin = Join-Path $mingwDirectory.FullName 'bin'
$qtBin = Join-Path $qtDirectory 'bin'
$cmakeBin = Split-Path -Parent $cmakeExe
$ninjaDirectory = Split-Path -Parent $ninjaExe
$presetPath = Join-Path $projectRoot 'CMakeUserPresets.json'
$preset = [ordered]@{
    version = 3
    configurePresets = @(
        [ordered]@{
            name = 'local-qt-debug'
            displayName = "Local Qt $QtVersion MinGW Debug"
            inherits = 'debug'
            environment = [ordered]@{
                PATH = (($mingwBin, $qtBin, $cmakeBin, $ninjaDirectory) -join ';') + ';$penv{PATH}'
            }
            cacheVariables = [ordered]@{
                CMAKE_PREFIX_PATH = $qtDirectory.Replace('\', '/')
                CMAKE_CXX_COMPILER = (Join-Path $mingwBin 'g++.exe').Replace('\', '/')
                CMAKE_MAKE_PROGRAM = $ninjaExe.Replace('\', '/')
            }
        }
    )
    buildPresets = @(
        [ordered]@{
            name = 'local-qt-debug'
            configurePreset = 'local-qt-debug'
            jobs = 0
        }
    )
}
$preset | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $presetPath -Encoding utf8

Write-Host ''
Write-Host 'Environment is ready.' -ForegroundColor Green
Write-Host "Qt:       $qtDirectory"
Write-Host "Compiler: $($mingwDirectory.FullName)"
Write-Host "CMake:    $cmakeExe"
Write-Host "Ninja:    $ninjaExe"
Write-Host "Preset:   $presetPath"

if ($Build) {
    Push-Location $projectRoot
    try {
        Invoke-Checked $cmakeExe @('--preset', 'local-qt-debug')
        Invoke-Checked $cmakeExe @('--build', '--preset', 'local-qt-debug')
        if ($RunTests) {
            $ctestExe = Join-Path $cmakeBin 'ctest.exe'
            Invoke-Checked $ctestExe @(
                '--test-dir', 'build/local-qt-debug', '--output-on-failure'
            )
        }
    } finally {
        Pop-Location
    }
}

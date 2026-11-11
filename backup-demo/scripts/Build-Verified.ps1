[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$QtVersion = '6.8.3',
    [string]$CMakeExe,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot '../..'))
if (-not $QtRoot) {
    $QtRoot = @((Join-Path $workspaceRoot 'work/Qt'), 'C:\Qt', (Join-Path $env:USERPROFILE 'Qt')) |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ "$QtVersion/mingw_64/bin/Qt6Core.dll") } |
        Select-Object -First 1
}
if (-not $QtRoot) { throw 'Qt not found. Pass -QtRoot with the directory containing the Qt version and Tools folders.' }
if (-not $CMakeExe) {
    $bundledCmake = Join-Path $workspaceRoot 'work/build-tools/cmake/data/bin/cmake.exe'
    if (Test-Path -LiteralPath $bundledCmake) { $CMakeExe = $bundledCmake }
    else { $CMakeExe = (Get-Command cmake.exe -ErrorAction Stop).Source }
}
$CMakeExe = (Resolve-Path -LiteralPath $CMakeExe).Path
$cmakeRoot = Split-Path -Parent (Split-Path -Parent $CMakeExe)
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$drives = @('R:', 'S:', 'T:', 'U:', 'V:', 'W:', 'X:', 'Y:', 'Z:') |
    Where-Object { -not (Test-Path ($_ + '\')) } | Select-Object -First 3
if ($drives.Count -ne 3) { throw 'Three unused drive letters are required for a Unicode-safe MinGW build.' }
$sourceDrive, $qtDrive, $cmakeDrive = $drives
$mapped = @()
$savedPath = $env:PATH
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
function Invoke-Checked([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed: $LASTEXITCODE" }
}
try {
    Invoke-Checked 'subst.exe' @($sourceDrive, $projectRoot)
    $mapped += $sourceDrive
    Invoke-Checked 'subst.exe' @($qtDrive, $QtRoot)
    $mapped += $qtDrive
    Invoke-Checked 'subst.exe' @($cmakeDrive, $cmakeRoot)
    $mapped += $cmakeDrive
    $CMakeExe = $cmakeDrive + '/bin/cmake.exe'
    $ctestExe = $cmakeDrive + '/bin/ctest.exe'
    $sourcePrefix = $sourceDrive + '/'
    $qtPrefix = $qtDrive + "/$QtVersion/mingw_64"
    $compilerPrefix = $qtDrive + '/Tools/mingw1310_64'
    if (-not (Test-Path ($compilerPrefix + '/bin/g++.exe'))) {
        throw 'Qt MinGW 13.1 compiler not found in Tools/mingw1310_64.'
    }
    $env:PATH = "$compilerPrefix/bin;$qtPrefix/bin;$savedPath"
    $buildPath = $sourcePrefix + 'build/verified'
    Invoke-Checked $CMakeExe @('-S', $sourcePrefix, '-B', $buildPath, '-G', 'MinGW Makefiles',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$qtPrefix",
        "-DCMAKE_CXX_COMPILER=$compilerPrefix/bin/g++.exe",
        "-DCMAKE_MAKE_PROGRAM=$compilerPrefix/bin/mingw32-make.exe",
        '-DCMAKE_CXX_FLAGS=-no-canonical-prefixes')
    Invoke-Checked $CMakeExe @('--build', $buildPath, '--parallel', '4')
    if (-not $SkipTests) {
        $testTemp = Join-Path $projectRoot 'build/test-temp'
        New-Item -ItemType Directory -Force -Path $testTemp | Out-Null
        $env:TEMP = $testTemp
        $env:TMP = $testTemp
        Invoke-Checked $ctestExe @('--test-dir', $buildPath, '--output-on-failure',
            '--output-log', ($buildPath + '/test-results.txt'))
    }
    Write-Host "Ready: $projectRoot\build\verified\data_backup.exe"
} finally {
    $env:PATH = $savedPath
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
    foreach ($drive in $mapped) { & subst.exe $drive /D }
}

[CmdletBinding()]
param(
    [switch]$InstallMissing
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$setupScript = Join-Path $PSScriptRoot 'Setup-Environment.ps1'

& $setupScript -InstallMissing:$InstallMissing -Build -RunTests
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$executable = Join-Path $projectRoot 'build\local-qt-debug\data_backup.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build completed but the executable was not found: $executable"
}

Start-Process -FilePath $executable -WorkingDirectory (Split-Path -Parent $executable)

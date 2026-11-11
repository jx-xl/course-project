[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$handoffRoot = Join-Path $projectRoot 'handoff'
$packagesRoot = Join-Path $handoffRoot 'packages'
New-Item -ItemType Directory -Path $packagesRoot -Force | Out-Null

$stages = @{
    workspace = Join-Path $handoffRoot '.stage-workspace'
    bOwned = Join-Path $handoffRoot '.stage-b-owned'
    cOwned = Join-Path $handoffRoot '.stage-c-owned'
}

function Initialize-Stage([string]$path) {
    $resolved = [System.IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith(
            $projectRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe staging path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $resolved | Out-Null
}

function Copy-ProjectFile([string]$relativePath, [string]$stage) {
    $source = Join-Path $projectRoot $relativePath
    $destination = Join-Path $stage $relativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

foreach ($stage in $stages.Values) {
    Initialize-Stage $stage
}

Push-Location $projectRoot
try {
    $workspaceFiles = @(& rg --files --hidden) | Where-Object {
        $_ -notmatch '^\.git([\\/]|$)' -and
        $_ -ne 'CMakeUserPresets.json' -and
        $_ -notmatch '^handoff[\\/]\.stage-'
    }
    if ($LASTEXITCODE -ne 0 -or $workspaceFiles.Count -eq 0) {
        throw 'Could not enumerate workspace files with rg.'
    }

    foreach ($relativePath in $workspaceFiles) {
        Copy-ProjectFile $relativePath $stages.workspace
    }

    $bOwnedFiles = @(
        'include\backup\core\FilesystemBackupManager.h',
        'include\backup\core\FilesystemRestoreManager.h'
    ) + @(Get-ChildItem -LiteralPath 'src\core', 'tests\core' -File -Recurse |
        ForEach-Object { [System.IO.Path]::GetRelativePath($projectRoot, $_.FullName) })

    $cOwnedFiles = @(
        'include\backup\filter\DefaultFilterManager.h',
        'include\backup\scheduler\QtScheduler.h'
    ) + @(Get-ChildItem -LiteralPath `
            'include\backup\gui', 'src\gui', 'src\filter', 'src\scheduler', 'resources', `
            'tests\filter', 'tests\scheduler', 'tests\gui' -File -Recurse |
        ForEach-Object { [System.IO.Path]::GetRelativePath($projectRoot, $_.FullName) })

    foreach ($relativePath in $bOwnedFiles | Select-Object -Unique) {
        Copy-ProjectFile $relativePath $stages.bOwned
    }
    foreach ($relativePath in $cOwnedFiles | Select-Object -Unique) {
        Copy-ProjectFile $relativePath $stages.cOwned
    }

    Copy-Item -LiteralPath 'handoff\member-b-task.md' `
        -Destination (Join-Path $stages.bOwned 'MEMBER_TASK.md') -Force
    Copy-Item -LiteralPath 'handoff\member-c-task.md' `
        -Destination (Join-Path $stages.cOwned 'MEMBER_TASK.md') -Force

    $packageMap = @{
        'team-workspace.zip' = $stages.workspace
        'member-b-owned-files.zip' = $stages.bOwned
        'member-c-owned-files.zip' = $stages.cOwned
    }

    foreach ($obsoletePackage in @('member-b-workspace.zip', 'member-c-workspace.zip')) {
        $obsoleteArchive = Join-Path $packagesRoot $obsoletePackage
        if (Test-Path -LiteralPath $obsoleteArchive) {
            Remove-Item -LiteralPath $obsoleteArchive -Force
        }
    }

    foreach ($entry in $packageMap.GetEnumerator()) {
        $archive = Join-Path $packagesRoot $entry.Key
        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }
        Compress-Archive -Path (Join-Path $entry.Value '*') `
            -DestinationPath $archive -CompressionLevel Optimal
    }
} finally {
    Pop-Location
    foreach ($stage in $stages.Values) {
        if (Test-Path -LiteralPath $stage) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
    }
}

Get-ChildItem -LiteralPath $packagesRoot -File |
    Sort-Object Name |
    Select-Object Name, Length, LastWriteTime

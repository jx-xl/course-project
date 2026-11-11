[CmdletBinding()]
param([ValidateSet('Update','Reset')][string]$Mode = 'Update')
$ErrorActionPreference = 'Stop'
$sampleRoot = Join-Path $PSScriptRoot 'demo/incremental-source'
$unchangedFile = Join-Path $sampleRoot '不变.txt'
$changedFile = Join-Path $sampleRoot '修改.txt'
$utf8 = New-Object System.Text.UTF8Encoding($false)
if ($Mode -eq 'Reset') {
    [IO.Directory]::CreateDirectory($sampleRoot) | Out-Null
    [IO.File]::WriteAllText($unchangedFile, "这份文件不修改。`n", $utf8)
    [IO.File]::WriteAllText($changedFile, "版本1`n", $utf8)
    Write-Host 'Demo reset to version 1. Create a FULL backup next.'
} else {
    if (-not (Test-Path -LiteralPath $unchangedFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $changedFile -PathType Leaf)) {
        throw 'Demo files are missing. Run Reset-IncrementalDemo.cmd first.'
    }
    [IO.File]::WriteAllText($changedFile, "版本2：这是修改后的内容。`n", $utf8)
    Write-Host 'Only the changed file was updated to version 2.'
    Write-Host 'Keep the same source and repository, choose INCREMENTAL backup next.'
}
Write-Host $sampleRoot

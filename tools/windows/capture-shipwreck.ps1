param(
    [Parameter(Mandatory = $true)][string]$DataDir,
    [Parameter(Mandatory = $true)][string]$SaveDir,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$Executable = "",
    [int]$Frames = 800,
    [int]$Width = 1280,
    [int]$Height = 960,
    [switch]$ShowFps
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Executable) {
    $Executable = Join-Path $repo 'port\build\windows-x64-release\darkcloud.exe'
}
$exe = (Resolve-Path -LiteralPath $Executable).Path
$data = (Resolve-Path -LiteralPath $DataDir).Path
$save = (Resolve-Path -LiteralPath $SaveDir).Path
$capture = [System.IO.Path]::GetFullPath($Output)
$inputFile = Join-Path $PSScriptRoot 'shipwreck-capture.input'
$arguments = @(
    '--headless', '--offscreen', '--fast-load', '--jump', 'dungeon:2',
    '--frames', "$Frames", '--width', "$Width", '--height', "$Height",
    '--data', $data, '--save', $save,
    '--input', $inputFile, '--display-per-tick', '1',
    '--screenshot', $capture
)
if ($ShowFps) { $arguments += '--screenshot-fps' }
& $exe @arguments
if ($LASTEXITCODE -ne 0) { throw "Shipwreck capture failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $capture)) { throw "Shipwreck capture did not produce $capture" }
Write-Output $capture

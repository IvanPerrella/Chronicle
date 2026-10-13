param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'

$checkout = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$config = 'windows-x64-release'
if ($Arguments.Count -and $Arguments[0] -in @('windows', 'windows-x64', 'windows-x64-release')) {
    if ($Arguments[0] -eq 'windows-x64') { $config = 'windows-x64' }
    $Arguments = @($Arguments | Select-Object -Skip 1)
}
$build = Join-Path $checkout "port/build/$config"
& (Join-Path $checkout 'build.bat') $config
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$disc = @(
    (Join-Path $checkout 'rom/pal/Dark Cloud (PAL).iso'),
    (Join-Path $checkout 'rom/Dark Cloud (PAL).iso')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($disc) {
    $data = Join-Path $checkout 'data'
    $extractor = Join-Path $build 'dcdata.exe'
    $discInfo = Get-Item -LiteralPath $disc
    $extractorInfo = Get-Item -LiteralPath $extractor
    $want = "iso $($discInfo.Length) $($discInfo.LastWriteTimeUtc.Ticks)`ndcdata $($extractorInfo.Length) $($extractorInfo.LastWriteTimeUtc.Ticks)"
    $stamp = Join-Path $data '.kazarov-data'
    $have = if (Test-Path -LiteralPath $stamp) { (Get-Content -LiteralPath $stamp -Raw).Trim() } else { '' }
    if ($have -ne $want) {
        New-Item -ItemType Directory -Force $data | Out-Null
        & $extractor extract $disc $data
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        Set-Content -LiteralPath $stamp -Value $want
    }
}

Push-Location $checkout
try {
    $game = Join-Path $build 'darkcloud.exe'
    $originalMarker = $env:DC_STREAMLINE_DEVICE_MARKER
    $originalStreamline = $env:DC_STREAMLINE
    $marker = Join-Path ([IO.Path]::GetTempPath()) "chronicle-streamline-$([Guid]::NewGuid()).marker"
    try {
        $env:DC_STREAMLINE_DEVICE_MARKER = $marker
        & $game @Arguments
        $gameExitCode = $LASTEXITCODE
        # Retry only when the access violation occurred during Streamline's
        # Vulkan device initialization. An unrelated crash remains a failure.
        if ($gameExitCode -eq -1073741819 -and (Test-Path -LiteralPath $marker -PathType Leaf)) {
            Write-Warning 'Streamline Vulkan initialization crashed (0xC0000005). Retrying with native Vulkan.'
            $env:DC_STREAMLINE = '0'
            & $game @Arguments
            $gameExitCode = $LASTEXITCODE
        }
    } finally {
        $env:DC_STREAMLINE = $originalStreamline
        $env:DC_STREAMLINE_DEVICE_MARKER = $originalMarker
        Remove-Item -LiteralPath $marker -ErrorAction SilentlyContinue
    }
    if ($gameExitCode -ne 0) {
        [Console]::Error.WriteLine("Game exited with code $gameExitCode (0x$($gameExitCode.ToString('X8'))).")
    }
    exit $gameExitCode
} finally {
    Pop-Location
}

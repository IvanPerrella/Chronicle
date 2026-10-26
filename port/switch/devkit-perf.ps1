# Measures a scene of the libnx build on a Nintendo development unit: packages port/build/switch's darkcloud.elf
# as a test application NSP in a new directory, runs it with --perf-log while ControlTarget relays the lm log,
# and summarizes the last full one-second windows in the scene (ten title, five town). After -Frames ticks
# the run holds for its final capture, then terminates and restores operation and performance policies to auto.
#
#   port/switch/devkit-perf.ps1 -Scene norune -DataZip <PAL data.zip> -SdkRoot <NintendoSDK> -Target <serial>
#
# title starts at the title screen with no input. norune starts in Norune (the developer menu's e01, MapNo 0)
# and backs away for 75 ticks, turns towards the populated village, then makes a short camera sweep
# across its buildings. The 420-tick route measures the last five full one-second windows.
# queens and muska require explicit qualified scene arguments, input and frame count.
# Docker runs elf2nso from the chronicle-switch image; the NintendoSDK's MakeMeta, AuthoringTool, RunOnTarget
# and ControlTarget do the rest. data.zip is hard-linked into the package, so keep both on one volume.
# SaveRoot, SceneArgsOverride and InputOverride support qualified save/load routes. SeedSaveSource
# copies only saves/1/save.dat, saves/1/quests.json and saves/state.json into romfs:/fixture-save;
# omit it on repeat runs to reuse the isolated destination. The app refuses a nonempty seed destination.
# StageOnly prepares code/romfs/manifest and returns before package creation or any target command.
param(
    [Parameter(Mandatory)] [ValidateSet('title', 'norune', 'queens', 'muska')] [string]$Scene,
    [Parameter(Mandatory)] [string]$DataZip,
    [Parameter(Mandatory)] [string]$SdkRoot,
    [Parameter(Mandatory)] [string]$Target,
    [string]$Build = (Join-Path $PSScriptRoot '..\build\switch'),
    [string]$Elf = '',
    [string]$Candidate = '',
    [string]$OutputRoot = $env:TEMP,
    [ValidateSet('handheld', 'console')] [string]$Mode = 'handheld',
    [ValidateSet('normal', 'auto')] [string]$Performance = 'normal',
    [string]$ProgramId = '0x01004b9000490020',
    [string]$Extra = '',
    [string]$SaveRoot = 'sdmc:/switch/chronicle/save-perf',
    [string]$SceneArgsOverride = '',
    [string]$InputOverride = '',
    [string]$SeedSaveSource = '',
    [switch]$StageOnly,
    [int]$Frames = 0,
    [int]$MeasureWindows = 0
)
$ErrorActionPreference = 'Stop'
if ($Scene -in @('queens', 'muska') -and
    (!$SceneArgsOverride -or !$InputOverride -or $Frames -le 0)) {
    throw 'Queens and Muska require SceneArgsOverride, InputOverride and Frames for a qualified route'
}
if ($SaveRoot -notmatch '^sdmc:/switch/chronicle/[A-Za-z0-9_/-]+$' -or $SaveRoot.Contains('..')) {
    throw 'SaveRoot must be a simple sdmc:/switch/chronicle/ path without traversal'
}
if ($Extra -match '(^|\s)--(?:save|jump|input|seed-save)(?:\s|=|$)') {
    throw 'Use the dedicated save, scene, input and seed parameters instead of duplicate Extra arguments'
}
if ($SceneArgsOverride -match '(^|\s)--(?:save|input|seed-save)(?:\s|=|$)' -or
    ([regex]::Matches($SceneArgsOverride, '(^|\s)--jump(?:\s|=|$)').Count -gt 1)) {
    throw 'SceneArgsOverride may contain one jump and must not override save, input or seed arguments'
}
if ($InputOverride -and !(Test-Path -LiteralPath $InputOverride -PathType Leaf)) {
    throw "Missing input override: $InputOverride"
}
$seedFiles = @('saves/1/save.dat', 'saves/1/quests.json', 'saves/state.json')
if ($SeedSaveSource) {
    if ($SaveRoot -eq 'sdmc:/switch/chronicle/save-perf') {
        throw 'Seeding requires an explicit isolated SaveRoot; the app also refuses a nonempty destination'
    }
    $seedRoot = (Resolve-Path -LiteralPath $SeedSaveSource).Path
    $actualFiles = @(Get-ChildItem -LiteralPath $seedRoot -Recurse -File)
    if ($actualFiles.Count -ne $seedFiles.Count -or
        @(Get-ChildItem -LiteralPath $seedRoot -Recurse -Attributes ReparsePoint).Count -ne 0) {
        throw 'Seed source must contain only the three save files, with no config, cache or links'
    }
    foreach ($relative in $seedFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $seedRoot $relative) -PathType Leaf)) {
            throw "Missing seed file: $relative"
        }
    }
}
$tools = Join-Path $SdkRoot 'Tools\CommandLineTools'
$controlTarget = Join-Path $tools 'ControlTarget.exe'
function Invoke-ControlCleanup([string]$Step, [string[]]$Arguments) {
    try {
        $process = Start-Process -FilePath $controlTarget -ArgumentList $Arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $root "cleanup-$Step.out") `
            -RedirectStandardError (Join-Path $root "cleanup-$Step.err")
        if (!$process.WaitForExit(15000)) {
            $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" -ErrorAction SilentlyContinue)
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            foreach ($child in $children) { Stop-Process -Id $child.ProcessId -Force -ErrorAction SilentlyContinue }
            Write-Warning "target cleanup $Step timed out; logs preserved in $root"
        } elseif ($process.ExitCode -ne 0) {
            Write-Warning "target cleanup $Step failed with exit $($process.ExitCode); logs preserved in $root"
        }
    } catch {
        Write-Warning "target cleanup $Step failed: $_"
    }
}
$townInput = "0 128 255 128 128`n75 128 128 255 128`n150`n240 128 128 255 128`n255`n285 128 128 0 128`n300`n"
$scenes = @{
    title  = @{ Args = '--jump title'; Mode = 0; Input = "0`n"; Frames = 720 }
    norune = @{ Args = '--jump e01 --fast-load'; Mode = 2; Input = $townInput; Frames = 420 }
    queens = @{ Mode = 2 }
    muska  = @{ Mode = 2 }
}
$info = $scenes[$Scene]
$sceneArgs = if ($SceneArgsOverride) { $SceneArgsOverride } else { $info.Args }
if ($Frames -le 0) { $Frames = $info.Frames }
if ($MeasureWindows -le 0) { $MeasureWindows = if ($Scene -eq 'title') { 10 } else { 5 } }

$root = Join-Path $OutputRoot ("Chronicle-DevkitPerf-{0}-{1}" -f $Scene, (Get-Date -Format 'yyyyMMdd-HHmmss'))
$code = Join-Path $root 'code'
$romfs = Join-Path $root 'romfs'
New-Item -ItemType Directory -Path $code, $romfs | Out-Null
if ([string]::IsNullOrEmpty($Elf)) { $Elf = Join-Path $Build 'darkcloud.elf' }
Copy-Item -LiteralPath $Elf -Destination (Join-Path $root 'darkcloud.elf')
$manifest = [ordered]@{
    candidate = $Candidate
    created = (Get-Date -Format o)
    scene = $Scene
    target = $Target
    mode = $Mode
    performance = $Performance
    elf_source = [IO.Path]::GetFullPath($Elf)
    elf_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'darkcloud.elf') -Algorithm SHA256).Hash
    frames = $Frames
    measure_windows = $MeasureWindows
    extra = $Extra
    save_root = $SaveRoot
    scene_arguments = $sceneArgs
    input_source = $InputOverride
    seed_source = $SeedSaveSource
    stage_only = [bool]$StageOnly
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'run-manifest.json') -Encoding utf8
docker run --rm -v "${root}:/stage" chronicle-switch:latest elf2nso /stage/darkcloud.elf /stage/code/main
if ($LASTEXITCODE -ne 0) { throw 'elf2nso failed' }
$manifest.nso_sha256 = (Get-FileHash -LiteralPath (Join-Path $code 'main') -Algorithm SHA256).Hash
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'run-manifest.json') -Encoding utf8
Remove-Item -LiteralPath (Join-Path $root 'darkcloud.elf')

$meta = Join-Path $root 'Chronicle.nmeta'
$template = Join-Path $SdkRoot 'Samples\Sources\Applications\MoviePlayerSimple\MoviePlayerSimple.aarch64.lp64.nmeta'
$text = [IO.File]::ReadAllText($template).Replace('0x01004b9000490000', $ProgramId)
$text = $text.Replace('<Name>Name</Name>', "<Name>Chronicle $Scene</Name>").Replace('<Publisher>Publisher</Publisher>', '<Publisher>Chronicle</Publisher>')
$text = $text.Replace('../../../../Samples/Sources/Resources/Icon/NintendoSDK_Application.bmp', (Join-Path $SdkRoot 'Samples\Sources\Resources\Icon\NintendoSDK_Application.bmp'))
[IO.File]::WriteAllText($meta, $text, [Text.UTF8Encoding]::new($false))
$desc = Join-Path $SdkRoot 'Resources\SpecFiles\Desc\NX-NXFP2-a64\Application.autogen.desc'
$manifest.clkrst_access = $Extra -match '(^|\s)--env DC_PERF_CLOCKS=1(\s|$)'
if ($manifest.clkrst_access) {
    [xml]$descriptor = [IO.File]::ReadAllText($desc)
    foreach ($section in @($descriptor.Desc.SrvAccessControlDescriptor, $descriptor.Desc.Default.SrvAccessControlData)) {
        $entry = $descriptor.CreateElement('Entry')
        $server = $descriptor.CreateElement('IsServer')
        $server.InnerText = 'False'
        $name = $descriptor.CreateElement('Name')
        $name.InnerText = 'clkrst'
        [void]$entry.AppendChild($server)
        [void]$entry.AppendChild($name)
        [void]$section.AppendChild($entry)
    }
    $desc = Join-Path $root 'ClockTelemetry.autogen.desc'
    $descriptor.Save($desc)
}
$manifest.npdm_descriptor = $desc
$manifest.npdm_descriptor_sha256 = (Get-FileHash -LiteralPath $desc -Algorithm SHA256).Hash
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'run-manifest.json') -Encoding utf8
& (Join-Path $tools 'MakeMeta\MakeMeta.exe') --platform NX "--meta=$meta" "--desc=$desc" "-o=$(Join-Path $code 'main.npdm')" | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'MakeMeta failed' }

New-Item -ItemType HardLink -Path (Join-Path $romfs 'data.zip') -Target $DataZip | Out-Null
Copy-Item -LiteralPath (Join-Path $Build 'lang'), (Join-Path $Build 'glyphs') -Destination $romfs -Recurse
if ($InputOverride) {
    Copy-Item -LiteralPath $InputOverride -Destination (Join-Path $romfs 'scene.input')
} else {
    [IO.File]::WriteAllText((Join-Path $romfs 'scene.input'), $info.Input)
}
$seedArgument = ''
if ($SeedSaveSource) {
    $manifest.seed_files = @(foreach ($relative in $seedFiles) {
        $source = Join-Path $seedRoot $relative
        $destination = Join-Path (Join-Path $romfs 'fixture-save') $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $copyHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -ne $copyHash) { throw "Seed copy differs: $relative" }
        [ordered]@{ path = $relative; sha256 = $copyHash }
    })
    $seedArgument = '--seed-save romfs:/fixture-save '
}
$arguments = "--data romfs:/data --save $SaveRoot --perf-log --env DC_PRESENT_STATS=1 --env DC_PERF_HOLD=1 " +
    "$seedArgument$sceneArgs --input romfs:/scene.input --frames $Frames $Extra"
[IO.File]::WriteAllText((Join-Path $romfs 'args.txt'), $arguments, [Text.UTF8Encoding]::new($false))
$manifest.arguments = $arguments
$manifest.input_sha256 = (Get-FileHash -LiteralPath (Join-Path $romfs 'scene.input') -Algorithm SHA256).Hash
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'run-manifest.json') -Encoding utf8
if ($StageOnly) {
    Write-Output "Offline staging ready: $root (no package launch or target commands)"
    return
}
$package = Join-Path $root 'Chronicle.nsp'
& (Join-Path $tools 'AuthoringTool\AuthoringTool.exe') creatensp --platform NX --meta $meta --type Application --desc $desc `
    --program $code $romfs --ignore-nss-nrs-option --no-check-dir-warning -o $package | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'AuthoringTool creatensp failed' }

$log = Join-Path $root 'run.log'
& $controlTarget check-alive --target $Target | Out-Null
if ($LASTEXITCODE -ne 0) { throw "$Target is not reachable" }
& $controlTarget devmenu --target $Target -- debug set-operation-mode-policy $Mode | Out-Null
& $controlTarget devmenu --target $Target -- debug set-performance-mode-policy $Performance | Out-Null
$listener = Start-Process -FilePath $controlTarget -ArgumentList @('read-log', '--target', $Target, '--print-host-time', '-v') `
    -RedirectStandardOutput $log -RedirectStandardError (Join-Path $root 'read-log.err') -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 3
    $launcher = Start-Process -FilePath (Join-Path $tools 'RunOnTarget.exe') `
        -ArgumentList @(('"' + $package + '"'), '--target', $Target, '--print-host-time', '--no-wait') `
        -RedirectStandardOutput (Join-Path $root 'launch.log') -RedirectStandardError (Join-Path $root 'launch.err') `
        -PassThru -WindowStyle Hidden
    $launchDeadline = (Get-Date).AddSeconds(120)
    while (!$launcher.WaitForExit(1000)) {
        if ((Get-Date) -ge $launchDeadline) {
            Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
            throw "application launch timed out; see $(Join-Path $root 'launch.log')"
        }
    }
    if ($launcher.ExitCode -ne 0) { throw "application launch failed; see $(Join-Path $root 'launch.log')" }
    $deadline = (Get-Date).AddSeconds($Frames / 15 + 120)
    while ((Get-Date) -lt $deadline -and -not (Select-String -Path $log -Pattern 'present: \d+ ticks' -Quiet)) {
        Start-Sleep -Milliseconds 500
        if ($listener.HasExited -or (Select-String -Path $log -Pattern 'Received LogSessionEnd' -Quiet)) {
            throw "benchmark log stream ended before completion: $log"
        }
    }
    if (!(Select-String -Path $log -Pattern 'present: \d+ ticks' -Quiet)) {
        throw "benchmark did not complete: $log"
    }
    & $controlTarget take-screenshot --target $Target --full-path (Join-Path $root 'final.png') *> $null
} finally {
    Stop-Process -Id $listener.Id -Force -ErrorAction SilentlyContinue
    Invoke-ControlCleanup 'terminate' @('terminate', '--target', $Target)
    Invoke-ControlCleanup 'operation-auto' @('devmenu', '--target', $Target, '--', 'debug', 'set-operation-mode-policy', 'auto')
    Invoke-ControlCleanup 'performance-auto' @('devmenu', '--target', $Target, '--', 'debug', 'set-performance-mode-policy', 'auto')
}

$windows = @(Select-String -Path $log -Pattern "\] perf [\d.]+s mode $($info.Mode) " | ForEach-Object { $_.Line -replace '^.*\] perf ', 'perf ' })
$last = @($windows | Where-Object { $_ -match '^perf ([\d.]+)s ' -and [double]$Matches[1] -ge 1.0 } |
    Select-Object -Last $MeasureWindows)
. (Join-Path $PSScriptRoot 'devkit-perf-audit.ps1')
$windowAudit = Get-DevkitPerfWindowAudit -ReportLines $windows -WindowCount $MeasureWindows
Select-String -Path $log -Pattern '\] perf [\d.]+s', 'cpu ', 'frame phases', 'scheduler thread', 'process memory', 'integer throughput' |
    ForEach-Object { $_.Line } |
    Set-Content -LiteralPath (Join-Path $root 'all-application-tail-slot-reports.txt') -Encoding utf8
$ticks = 0; $late = 0; $seconds = 0.0; $gpu = 0.0
$maximumGpu = 0.0; $displayGpu = 0.0
foreach ($window in $last) {
    if ($window -match '^perf ([\d.]+)s .*ticks (\d+) late (\d+).*gpu canon ([\d.]+)') {
        $seconds += [double]$Matches[1]; $ticks += [int]$Matches[2]; $late += [int]$Matches[3]; $gpu += [double]$Matches[4]
    }
    if ($window -match 'gpu canon [\d.]+/([\d.]+) disp ([\d.]+)') {
        $maximumGpu = [Math]::Max($maximumGpu, [double]$Matches[1]); $displayGpu += [double]$Matches[2]
    }
}
"${Scene}, ${Mode}/${Performance}: ${root}"
'switch: operation', 'gfx: swapchain' | ForEach-Object { Select-String -Path $log -Pattern $_ | Select-Object -Last 1 } | ForEach-Object { $_.Line -replace '^.*\] ', '' }
if ($seconds -gt 0) {
    [ordered]@{
        candidate = $Candidate
        elf_sha256 = $manifest.elf_sha256
        scene = $Scene
        selected_full_windows = $last.Count
        seconds = [Math]::Round($seconds, 6)
        ticks = $ticks
        late_ticks = $late
        ticks_per_second = [Math]::Round($ticks / $seconds, 6)
        canonical_gpu_ms_mean_of_windows = [Math]::Round($gpu / $last.Count, 6)
        canonical_gpu_ms_maximum_sample = $maximumGpu
        display_gpu_ms_mean_of_windows = [Math]::Round($displayGpu / $last.Count, 6)
        windows = $last
        window_audit = $windowAudit
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'measurement-summary.json') -Encoding utf8
    '{0:N1} ticks/s over {1:N1} s, {2} late ticks, canonical GPU {3:N2} ms' -f ($ticks / $seconds), $seconds, $late, ($gpu / $last.Count)
    $last | Select-Object -Last 1
} else {
    throw "no perf windows in the requested scene; see $log"
}

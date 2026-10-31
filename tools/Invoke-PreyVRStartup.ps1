<#
.SYNOPSIS
    Starts VR in an injected Prey session through the mod's own bring-up, and
    reports the session it reached.

.DESCRIPTION
    `vr.enable` (src/app/VrMode.cpp) owns the bring-up and the one order that
    works:
    - gamma before the session, because it is a swapchain format fixed at
      creation, not a cvar;
    - Prey's own projection, and stereo at the runtime's IPD;
    - the near-pass FOV derived from the render aspect;
    - renderer settings verified by readback;
    - a recenter before head tracking, then controls, hands, the HUD layer and
      the UI panel.

    F11 in game sends the same thing. This script sends it from a shell, waits
    for the phase to reach active, and prints the resolution chain and state.

    It used to replay that sequence itself, command by command, which its own
    comment called "the real defect": two bring-up paths side by side drifted
    (a fixed 64 mm IPD, no dual stereo, no UI panel). The copy was removed on
    2026-10-18. Per-session tuning is a VR Options setting or a channel command
    after start (docs/TUNING-COMMANDS.md).

    What has to land before the session exists is still here: the render size
    and supersampling, because the XR swapchain is sized once from the
    backbuffer. The launcher sets the render size itself (PREYVR_RENDER_WIDTH
    and PREYVR_RENDER_HEIGHT); these are for a game started some other way.

.PARAMETER RunDir
    The launcher's run directory. Defaults to the run with the newest PreyVR.log.

.EXAMPLE
    ./tools/Invoke-PreyVRStartup.ps1
#>
[CmdletBinding()]
param(
    # Render size for the headset, sent before vr.enable starts the session. 0
    # leaves the game's own resolution alone.
    [int]$RenderWidth = 0,
    [int]$RenderHeight = 0,
    # 1 = off, 2 = 2x2 SSAA, 3 = 3x3. Genuine extra detail resolved down, unlike
    # upscaling. 2 is four times the scene pixels: check the frame time.
    [int]$Supersampling = 0,
    [string]$RunDir = '',
    # Direct channel directory, for a game not started by the launcher: the DLL
    # defaults to <Documents>\PreyVR, which has no run directory or 'log' subfolder.
    [string]$LogDir = '',
    # How much of the headset's view the flat mirror spans, as a percentage of
    # Prey's own declared field. The MENU measures at 41% of the view at 100 and
    # 83% at 200 (R-113), so this is the knob that makes menus readable. It only
    # affects frames with no held eye pair; the stereo path uses the runtime's
    # own per-eye field and ignores it.
    [int]$MirrorFov = 0,
    # vr.enable waits up to two minutes for the headset to be put on.
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'PreyVRCommon.ps1')
if (-not $LogDir) { $LogDir = Find-PreyVRChannel $RunDir }
$log = $LogDir
if (-not (Test-Path -LiteralPath $log)) { Write-Error "no channel at $log"; exit 1 }
Write-Host "channel: $log"

function Send-Cmd($line) { Send-PreyVRCommand $log $line }

$failures = 0
function Step($label, $line, [switch]$AllowNonZero) {
    $reply = Send-Cmd $line
    # A status verb prints status=name(n); a result verb prints result=n. Only the
    # latter is a failure when non-zero -- reporting a status as an error is a
    # mistake this project has already made three times.
    $bad = $false
    if (-not $AllowNonZero -and $reply -match 'result=(\d+)' -and [int]$Matches[1] -ne 0) { $bad = $true }
    # **But some statuses ARE failures, and printing them as ok is how a dead
    # session read as a good start.** On 2026-09-10 this reported
    # "ok  xr.start  status=unavailable(3)" -- the runtime had no headset
    # (XR_ERROR_FORM_FACTOR_UNAVAILABLE) and every later step was meaningless,
    # yet the script said it was fine.
    #
    # Judged by NAME, not number, and by an explicit bad list rather than a good
    # one: a name nobody has seen yet should not be failed on sight, but these
    # four are unambiguous.
    if ($reply -match 'status=([a-z_]+)\(' -and
        $Matches[1] -in @('unavailable', 'failed', 'refused', 'stopped')) { $bad = $true }
    if ($reply -eq '(no result)') { $bad = $true }
    if ($bad) { $script:failures++ ; Write-Host ("  FAIL {0,-22} {1}" -f $label, $reply) }
    else { Write-Host ("  ok   {0,-22} {1}" -f $label, $reply) }
}

if (($RenderWidth -gt 0 -and $RenderHeight -gt 0) -or $Supersampling -gt 0) {
    Write-Host ''
    Write-Host 'before the session exists -- these cannot be changed afterwards:'
    # The console queue holds exactly one command, so these are spaced.
    if ($RenderWidth -gt 0 -and $RenderHeight -gt 0) {
        Step 'render width'  ("console r_Width {0}" -f $RenderWidth)
        Start-Sleep -Milliseconds 800
        Step 'render height' ("console r_Height {0}" -f $RenderHeight)
        Start-Sleep -Milliseconds 800
    }
    if ($Supersampling -gt 0) {
        Step 'supersampling' ("console r_Supersampling {0}" -f $Supersampling)
        Start-Sleep -Milliseconds 800
    }
}

Write-Host ''
Write-Host 'VR start -- put the headset on, facing forward:'
# vr.enable reports a PHASE, not a result, and 'preparing' is the normal first
# answer: it advances on the command worker's own tick. So the phase is polled
# until it settles, rather than judged from the first reply.
Step 'vr.enable' 'vr.enable' -AllowNonZero
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
do {
    Start-Sleep -Seconds 2
    $vr = Send-Cmd 'vr.status'
} while ($vr -notmatch 'vr=(active|failed|off)\b' -and (Get-Date) -lt $deadline)
if ($vr -match 'vr=active\b') {
    Write-Host "  ok   VR mode active         $vr"
} else {
    $failures++
    Write-Host "  FAIL VR mode not active      $vr"
}

if ($MirrorFov -gt 0) {
    Write-Host ''
    Write-Host 'flat mirror field:'
    Step 'mirror fov' "xr.mirrorfov $MirrorFov"
}

Write-Host ''
$resolution = Send-Cmd 'xr.resolution'
Write-Host 'resolution chain:'
Write-Host ("  " + $resolution)
Write-Host ''
$report = Send-Cmd 'report'
$want = 'xrSession|xrFrames|viewApplied|posApplied|nearApplied|cameraEdit|lastEye|ikCal'
Write-Host 'state:'
($report -split ' ' | Where-Object { $_ -match $want }) | ForEach-Object { Write-Host "  $_" }

Write-Host ''
if ($failures -gt 0) {
    Write-Host ("startup completed with {0} failed step(s) -- read the FAIL lines above" -f $failures)
    exit 1
}
Write-Host 'startup complete.'
Write-Host ''
Write-Host 'Next: load a save and EQUIP A WEAPON, then run Invoke-PreyVRIkTest.ps1.'
Write-Host '  - vr.enable arms the controls, hands, reticle and HUD itself, and'
Write-Host '    calibrates the arms when the player rig binds and after each recenter.'
Write-Host '  - Judge the reticle by reticleDispatched climbing with'
Write-Host '    reticleDispatchFailed at zero, THEN by eye. A dispatch returning 0'
Write-Host '    proves the ABI, not that the movie has that function (F-011).'
Write-Host '  - -RenderHeight 2880 -RenderWidth 2688 matches the runtime request for'
Write-Host '    a game not started by the launcher; it must land before the session.'

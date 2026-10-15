[CmdletBinding()]
param(
    [string]$GameExe='',
    [string]$PackageDir='',
    [string]$Runtime='',
    [string]$RunRoot="$env:LOCALAPPDATA\PreyVR\runs",
    [int]$Width=2016,
    [int]$Height=2160,
    # **Launch settings only.** Everything the VR Options menu shows -- turning,
    # movement direction, interface size, curvature, the controls guide, laser
    # hand, HUD style, the debug overlay, shots from the muzzle -- is the menu's,
    # saved in %LOCALAPPDATA%\PreyVR\vr-options.ini.
    # auto prefers LOCAL_FLOOR, then STAGE, with LOCAL fallback.
    [string]$ReferenceSpace='auto',
    # **Both eyes from one game frame (1) or alternate eyes (0).** 1 is the
    # route that removed the doubling a wearer saw in motion; 0 is the earlier
    # path that renders one eye per frame. Kept as an escape hatch for problems
    # specific to the both-eyes path (STEREO-DUAL-SUBMISSION-2026-10-10.md).
    # Read once when VR starts, as PREYVR_DUAL_STEREO.
    [int]$DualStereo=1,
    [switch]$DryRun
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Resolve-PreyVRRenderSize {
    param($Saved, [int]$Width, [int]$Height, [bool]$WidthExplicit, [bool]$HeightExplicit)
    if ($WidthExplicit -ne $HeightExplicit) {
        throw 'Set both -Width and -Height together; a partial override changes the eye aspect ratio.'
    }
    $migrate=$false
    if ($Saved -and !$WidthExplicit) {
        if ($Saved.PSObject.Properties['Width']) {$Width=[int]$Saved.Width}
        if ($Saved.PSObject.Properties['Height']) {$Height=[int]$Saved.Height}
        # v0.5.0 persisted its widescreen default without recording whether the
        # user chose it. Migrate only that exact unversioned pair. Explicit CLI
        # dimensions, custom saved sizes and versioned settings remain choices.
        $migrate=!$Saved.PSObject.Properties['ResolutionDefaultsVersion'] -and
            $Saved.PSObject.Properties['Width'] -and $Saved.PSObject.Properties['Height'] -and
            $Width -eq 2560 -and $Height -eq 1440
        if ($migrate) {$Width=2016;$Height=2160}
    }
    if ($Width -lt 1280 -or $Width -gt 8192 -or $Height -lt 720 -or $Height -gt 8192) {
        throw 'Render size must be 1280..8192 by 720..8192.'
    }
    [pscustomobject]@{Width=$Width;Height=$Height;MigrateLegacy=$migrate}
}

function Resolve-PreyVRReferenceSpace {
    param($Saved, $Overrides, [string]$ReferenceSpace)
    if ($Saved -and !$Overrides.ContainsKey('ReferenceSpace') -and $Saved.PSObject.Properties['ReferenceSpace']) {$ReferenceSpace=[string]$Saved.ReferenceSpace}
    if ($ReferenceSpace -notin @('auto','local')) {throw 'ReferenceSpace must be auto (prefer floor) or local.'}
    $ReferenceSpace.ToLowerInvariant()
}

function Resolve-PreyVRStereoSettings {
    param($Saved, $Overrides, [int]$DualStereo)
    if ($Saved -and !$Overrides.ContainsKey('DualStereo') -and $Saved.PSObject.Properties['DualStereo']) {$DualStereo=[int]$Saved.DualStereo}
    if ($DualStereo -notin @(0,1)) {throw 'DualStereo must be 1 (both eyes per frame) or 0 (alternate eyes).'}
    $DualStereo
}

function Get-PreyVRStereoStatus {
    param([string]$LogText)
    # VrMode logs one dual_stereo line per VR start, before state=active: on,
    # off (PREYVR_DUAL_STEREO=0) or unavailable (hooks refused; alternate eyes).
    # A fallback is otherwise silent, so the launcher reports what came up.
    $found=[regex]::Matches($LogText,'preyvr_vr dual_stereo=([a-z]+)')
    if (!$found.Count) {return 'unknown'}
    $found[$found.Count-1].Groups[1].Value
}

function Get-PreyVRStartupCommands {
    # One ordered batch, before enabling VR. Successive writes separated by a
    # guessed sleep can overwrite commands while the channel is busy. Nothing
    # the VR Options menu owns is sent: VR start applies vr-options.ini.
    'input.hotkeys 1'
    'vr.enable'
}

function Save-PreyVRConfiguration {
    param([string]$Path, $Saved, [hashtable]$Settings, [bool]$MigrateLegacy)
    if ($MigrateLegacy) {
        $backup=$Path+'.before-resolution-fix-'+[guid]::NewGuid().ToString('N')+'.bak'
        Copy-Item -LiteralPath $Path -Destination $backup -ErrorAction Stop
        Write-Host "Previous display settings backed up to $backup"
    }
    if (!$Saved) {$Saved=[pscustomobject]@{}}
    foreach ($key in $Settings.Keys) {$Saved | Add-Member -NotePropertyName $key -NotePropertyValue $Settings[$key] -Force}
    if (!$Saved.PSObject.Properties['ResolutionDefaultsVersion']) {
        $Saved | Add-Member -NotePropertyName 'ResolutionDefaultsVersion' -NotePropertyValue 1
    }
    $Saved | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-PreyVRWindowFit {
    param([int]$AreaLeft, [int]$AreaTop, [int]$AreaWidth, [int]$AreaHeight,
        [int]$FrameWidth, [int]$FrameHeight, [int]$RenderWidth, [int]$RenderHeight)
    # **The desktop window is the eye size, which no desktop is.** Windowed,
    # Prey sizes its window from r_Width x r_Height and centres it on the
    # screen. At 2688 x 2880 on a 1440-high desktop that centre is -739, and
    # Windows then clamps the height to the screen: the window ended halfway
    # down with its title bar and top half above the screen.
    #
    # The backbuffer -- what the headset gets -- does not follow the window.
    # Measured 2026-10-15: after resizing the window to 1263 x 1353 the frame
    # capture still read 2688 x 2880, and DXGI scales into the smaller client.
    # So this fits the window inside the work area at the eye's aspect ratio,
    # never larger than the render size, and centres it.
    # Integer arithmetic, so the limiting side fills the area exactly rather
    # than a pixel short of it.
    $roomWidth=[long]($AreaWidth-$FrameWidth);$roomHeight=[long]($AreaHeight-$FrameHeight)
    if ($RenderWidth -le $roomWidth -and $RenderHeight -le $roomHeight) {
        $clientWidth=$RenderWidth;$clientHeight=$RenderHeight
    } elseif ($RenderWidth*$roomHeight -le $RenderHeight*$roomWidth) {
        $clientHeight=$roomHeight;$clientWidth=[math]::Floor($RenderWidth*$roomHeight/$RenderHeight)
    } else {
        $clientWidth=$roomWidth;$clientHeight=[math]::Floor($RenderHeight*$roomWidth/$RenderWidth)
    }
    $clientWidth=[int][math]::Max(1,$clientWidth);$clientHeight=[int][math]::Max(1,$clientHeight)
    $width=$clientWidth+$FrameWidth;$height=$clientHeight+$FrameHeight
    [pscustomobject]@{
        X=$AreaLeft+[int][math]::Floor(($AreaWidth-$width)/2)
        Y=$AreaTop+[int][math]::Floor(($AreaHeight-$height)/2)
        Width=$width;Height=$height;ClientWidth=$clientWidth;ClientHeight=$clientHeight
    }
}

function Set-PreyVRWindowFit {
    param([IntPtr]$Window, [int]$RenderWidth, [int]$RenderHeight)
    if (!('PreyVRWindow' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PreyVRWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
    }
    # Physical pixels on both sides: a DPI-unaware launcher at 125% would read
    # a scaled work area and place the window by it. Per-monitor v2 is -4.
    $previous=[IntPtr]::Zero
    try {$previous=[PreyVRWindow]::SetThreadDpiAwarenessContext([IntPtr](-4))} catch {}
    try {
        $outer=New-Object PreyVRWindow+Rect;$client=New-Object PreyVRWindow+Rect
        if (![PreyVRWindow]::GetWindowRect($Window,[ref]$outer) -or ![PreyVRWindow]::GetClientRect($Window,[ref]$client)) {
            return 'Desktop window: not found, left where Prey put it.'
        }
        $info=New-Object PreyVRWindow+MonitorInfo;$info.Size=[Runtime.InteropServices.Marshal]::SizeOf($info)
        if (![PreyVRWindow]::GetMonitorInfo([PreyVRWindow]::MonitorFromWindow($Window,2),[ref]$info)) {
            return 'Desktop window: no monitor information, left where Prey put it.'
        }
        # The frame is measured, not computed from the style: whatever borders
        # this window has now are the ones it keeps.
        $fit=Get-PreyVRWindowFit $info.Work.Left $info.Work.Top ($info.Work.Right-$info.Work.Left) ($info.Work.Bottom-$info.Work.Top) `
            (($outer.Right-$outer.Left)-$client.Right) (($outer.Bottom-$outer.Top)-$client.Bottom) $RenderWidth $RenderHeight
        # SWP_NOZORDER | SWP_NOACTIVATE: move and size only.
        if (![PreyVRWindow]::SetWindowPos($Window,[IntPtr]::Zero,$fit.X,$fit.Y,$fit.Width,$fit.Height,0x14)) {
            return "Desktop window: could not be moved (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
        }
        "Desktop window: $($fit.ClientWidth) x $($fit.ClientHeight), fitted to the screen. The headset still gets $RenderWidth x $RenderHeight."
    } finally {
        if ($previous -ne [IntPtr]::Zero) {[void][PreyVRWindow]::SetThreadDpiAwarenessContext($previous)}
    }
}

# **`$PSScriptRoot` must not be a param default here.** Under Windows PowerShell
# 5.1, a script with [CmdletBinding()] invoked as `powershell.exe -File`
# evaluates param defaults in a scope where $PSScriptRoot is still empty --
# while $PSScriptRoot in the body is correct. PowerShell 7 does not do this, so
# the bug is invisible to anyone testing with pwsh.
#
# The shipped .cmd wrapper uses powershell.exe, so EVERY double-click hit it:
# $PackageDir came out empty and the first Join-Path threw "Cannot bind argument
# to parameter 'Path' because it is an empty string" before anything launched.
# Resolved in the body instead, which behaves identically in both versions.
if (-not $PackageDir) { $PackageDir = $PSScriptRoot }
if (-not $PackageDir) { $PackageDir = Split-Path -Parent $PSCommandPath }
try {
    $config=Join-Path $PackageDir 'PreyVR.json'
    $saved=$null
    if (Test-Path -LiteralPath $config) {
        $saved=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
        if (!$GameExe -and $saved.PSObject.Properties['GameExe']) {$GameExe=$saved.GameExe}
    }
    $options=Join-Path $env:LOCALAPPDATA 'PreyVR\vr-options.ini'
    $renderSize=Resolve-PreyVRRenderSize -Saved $saved -Width $Width -Height $Height `
        -WidthExplicit $PSBoundParameters.ContainsKey('Width') -HeightExplicit $PSBoundParameters.ContainsKey('Height')
    $Width=$renderSize.Width;$Height=$renderSize.Height
    $ReferenceSpace=Resolve-PreyVRReferenceSpace $saved $PSBoundParameters $ReferenceSpace
    $DualStereo=Resolve-PreyVRStereoSettings $saved $PSBoundParameters $DualStereo
    if (!$GameExe) {
        $candidates=@()
        foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 480490',
                           'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 480490')) {
            $install=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
            if ($install -and $install.InstallLocation) {
                $candidates+=Join-Path $install.InstallLocation 'Binaries\Danielle\x64\Release\Prey.exe'
            }
        }
        $GameExe=$candidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
    }
    if (!$GameExe -or !(Test-Path -LiteralPath $GameExe)) {
        if ($DryRun) {throw 'Supply -GameExe for this preflight.'}
        Add-Type -AssemblyName System.Windows.Forms
        $picker=New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title='Choose Prey.exe (Prey 2017, Steam version)'
        $picker.Filter='Prey executable|Prey.exe'
        if ($picker.ShowDialog() -ne 'OK') {exit 0}
        $GameExe=$picker.FileName
    }
    $GameExe=(Resolve-Path -LiteralPath $GameExe).Path
    $release=Split-Path -Parent $GameExe
    $gameDll=Join-Path $release 'PreyDll.dll'
    $supported='7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7'
    if ((Get-FileHash -LiteralPath $gameDll -Algorithm SHA256).Hash -ne $supported) {
        throw 'This Prey build is not supported by this package. No mod was loaded.'
    }
    # **The binaries are not always beside this script.** In the player package
    # they are. In the repo, tools/ holds the launcher and nothing else, so a
    # wearer who opened tools\ in Explorer and double-clicked "Start Prey VR.cmd"
    # -- an identical-looking file to the packaged one -- got
    # "Package file missing: PreyVR.dll" and no way to act on it.
    #
    # Fall back to the build output, and print which directory won either way,
    # because the failure mode this replaces was two launchers that look the
    # same and behave differently.
    $needed=@('PreyVR.dll','openxr_loader.dll','preyvr_injector.exe')
    $BinDir=$null
    foreach ($candidate in @($PackageDir,(Join-Path $PackageDir '../build/headless/Release'))) {
        $present=@($needed | Where-Object {Test-Path -LiteralPath (Join-Path $candidate $_)})
        if ($present.Count -eq $needed.Count) {$BinDir=(Resolve-Path -LiteralPath $candidate).Path;break}
    }
    if (!$BinDir) {
        throw ("Mod binaries not found beside {0} or in its build output. Missing: {1}. Run the launcher from the player package, or build first." -f
            $PackageDir,(($needed | Where-Object {!(Test-Path -LiteralPath (Join-Path $PackageDir $_))}) -join ', '))
    }
    if ($Runtime -and !(Test-Path -LiteralPath $Runtime)) {throw 'OpenXR runtime manifest does not exist.'}
    # Never boot another fleet game over an active test session.
    $fleetNames=@('Prey','Dishonored','Bioshock','BioshockHD','FarCry2','Soma','Shock2',
        'SystemShock2','SystemShock2Remastered','Swat4','Swat4X','SoF','TS4_x64','moh','moh_s','Il-2','Launcher64','carrier_command_2')
    $running=@(Get-Process -Name $fleetNames -ErrorAction SilentlyContinue)
    if ($running.Count) {throw ('Close the running game first: '+(($running | Select-Object -ExpandProperty ProcessName) -join ', '))}
    Write-Host "Prey VR: $Width x $Height per eye; reference $ReferenceSpace; stereo $(if($DualStereo){'both eyes per frame'}else{'alternate eyes'}); runtime: $(if($Runtime){$Runtime}else{'system OpenXR runtime'})"
    if ($renderSize.MigrateLegacy) {Write-Host 'Replacing the v0.5.0 widescreen default with 2016 x 2160 for greater vertical view coverage.'}
    Write-Host "Mod binaries: $BinDir"
    # The in-game settings are the VR Options menu's: say so once.
    Write-Host "In-game settings: VR Options (hold the left menu button), saved in $options"
    Write-Host 'Controls: menu button = pause; right stick = navigate; A = select; B = back.'
    Write-Host 'Point + beam-hand trigger = click/drag; grips = tabs; X/Y = actions.'
    Write-Host 'F12 or both grips + left Y = reset view. Press A at the title/loading prompt.'
    Write-Host 'Tilde (the key below Esc) opens Prey''s own console, when focused.'
    if ($DryRun) {Write-Host 'Preflight passed. Nothing launched.';exit 0}
    Save-PreyVRConfiguration -Path $config -Saved $saved -MigrateLegacy $renderSize.MigrateLegacy -Settings `
        @{GameExe=$GameExe;Width=$Width;Height=$Height;ReferenceSpace=$ReferenceSpace;DualStereo=$DualStereo}
    $run=Join-Path $RunRoot ('player-{0:yyyyMMdd-HHmmss}' -f (Get-Date))
    [void](New-Item -ItemType Directory -Path $run -Force)
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=$GameExe
    $info.WorkingDirectory=(Get-Item -LiteralPath $release).Parent.Parent.Parent.Parent.FullName
    $info.UseShellExecute=$false
    $info.Arguments="+r_Fullscreen 0 +r_Width $Width +r_Height $Height"
    $info.EnvironmentVariables['SteamAppId']='480490'
    $info.EnvironmentVariables['PREYVR_LOG_PATH']=Join-Path $run 'PreyVR.log'
    $info.EnvironmentVariables['PREYVR_RENDER_WIDTH']="$Width"
    $info.EnvironmentVariables['PREYVR_RENDER_HEIGHT']="$Height"
    $info.EnvironmentVariables['PREYVR_REFERENCE_SPACE']=$ReferenceSpace
    $info.EnvironmentVariables['PREYVR_DUAL_STEREO']="$DualStereo"
    # Runtime overrides and simulator state remain process-scoped. The normal
    # double-click path uses the player's selected runtime (VDXR/SteamVR/etc.).
    if ($Runtime) {$info.EnvironmentVariables['XR_RUNTIME_JSON']=(Resolve-Path -LiteralPath $Runtime).Path}
    $process=[System.Diagnostics.Process]::Start($info)
    Write-Host "Starting Prey (PID $($process.Id))..."
    $deadline=(Get-Date).AddSeconds(120)
    $ready=$false
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) {throw 'Prey exited or Steam replaced the process. Start Steam and retry.'}
        try {
            $process.Refresh()
            $ready=($process.MainWindowHandle -ne 0) -and
                (@($process.Modules | Where-Object {$_.ModuleName -eq 'PreyDll.dll'}).Count -eq 1)
        } catch {$ready=$false}
        if ($ready) {break}
        Start-Sleep -Milliseconds 250
    }
    if (!$ready) {throw 'Timed out waiting for Prey to initialize. The game is left running.'}
    # Allow the window/device initialization to finish before the signature gate.
    Start-Sleep -Seconds 3
    & (Join-Path $BinDir 'preyvr_injector.exe') $process.Id (Join-Path $BinDir 'PreyVR.dll')
    if ($LASTEXITCODE -ne 0) {throw "Mod load failed (injector code $LASTEXITCODE)."}
    $log=Join-Path $run 'PreyVR.log'
    $deadline=(Get-Date).AddSeconds(20)
    do {
        if ((Test-Path -LiteralPath $log) -and
            (Select-String -LiteralPath $log -SimpleMatch 'preyvr_channel result=0 detail=started' -Quiet)) {break}
        if ((Get-Date) -gt $deadline) {throw "Mod startup did not finish. See $log"}
        Start-Sleep -Milliseconds 200
    } while ($true)
    # The renderer has settled by now, so Prey will not place the window again.
    # A failure here costs the desktop view only, never the launch.
    try {Write-Host (Set-PreyVRWindowFit $process.MainWindowHandle $Width $Height)}
    catch {Write-Host "Desktop window: left where Prey put it ($($_.Exception.Message))." -ForegroundColor Yellow}
    Get-PreyVRStartupCommands |
        Set-Content -LiteralPath (Join-Path $run 'commands.txt') -Encoding ASCII
    $deadline=(Get-Date).AddSeconds(150)
    Write-Host 'Put on the headset. Enabling VR...'
    do {
        $text=Get-Content -LiteralPath $log -Raw
        if ($text -match 'preyvr_vr state=failed step=([^\s]+)') {throw "VR setup failed at $($Matches[1]). See $log (F11 retries)."}
        if ($text -match 'preyvr_vr state=active') {break}
        if ($process.HasExited) {throw 'Prey closed during VR setup.'}
        if ((Get-Date) -gt $deadline) {throw "Waiting for VR timed out. See $log"}
        Start-Sleep -Milliseconds 250
    } while ($true)
    Write-Host "VR is ready. Diagnostics: $run"
    switch (Get-PreyVRStereoStatus $text) {
        'on' {Write-Host 'Stereo: both eyes from each frame.'}
        'off' {Write-Host 'Stereo: alternate eyes (DualStereo 0).'}
        'unavailable' {Write-Host 'Stereo: both-eyes rendering was unavailable; using alternate eyes. See the log.' -ForegroundColor Yellow}
        default {Write-Host 'Stereo: the log did not report a mode.' -ForegroundColor Yellow}
    }
    Write-Host ''
    Write-Host 'To adjust while playing, run "Tune Prey VR.cmd" beside this launcher'
    Write-Host 'and type e.g.  ui.scale 130   (20..200, current session only).'
    Write-Host 'VR Options saves the in-game settings; PreyVR.json keeps the launch settings.'
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if (!$DryRun) {Read-Host 'Press Enter to close' | Out-Null}
    exit 1
}

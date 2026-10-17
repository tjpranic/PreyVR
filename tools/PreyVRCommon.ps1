# Shared by the development scripts here; dot-source it:
#   . (Join-Path $PSScriptRoot 'PreyVRCommon.ps1')
#
# Where the game and xr-sim are, and the mod's file command channel. Start-PreyVR.ps1
# and Send-PreyVR.ps1 keep their own copies: they ship alone in the player package.
# Windows PowerShell 5.1 syntax throughout.

# The Prey install (...\steamapps\common\Prey): Steam's uninstall entry, then the
# upstream library on D:, then every library in Steam's own list. The first that
# holds PreyDll.dll wins; with none, the upstream path, so a message names it.
function Resolve-PreyInstall {
    $relative = 'Binaries\Danielle\x64\Release\PreyDll.dll'
    $upstream = 'D:\SteamLibrary\steamapps\common\Prey'
    $candidates = @()
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 480490',
                       'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 480490')) {
        $install = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($install -and $install.PSObject.Properties['InstallLocation'] -and $install.InstallLocation) {
            $candidates += $install.InstallLocation
        }
    }
    $candidates += $upstream
    # Steam leaves the uninstall entry empty on some machines; its library list is always kept.
    $steam = Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if ($steam -and $steam.PSObject.Properties['SteamPath'] -and $steam.SteamPath) {
        $folders = Join-Path $steam.SteamPath 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $folders) {
            foreach ($match in [regex]::Matches((Get-Content -LiteralPath $folders -Raw), '"path"\s+"([^"]+)"')) {
                $candidates += Join-Path ($match.Groups[1].Value -replace '\\\\', '\') 'steamapps\common\Prey'
            }
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate $relative)) { return $candidate }
    }
    return $upstream
}

# The xr-sim checkout (its tools\ scripts): PREYVR_XRSIM_ROOT, else upstream's.
function Resolve-XrSimRoot {
    if ($env:PREYVR_XRSIM_ROOT) { return $env:PREYVR_XRSIM_ROOT }
    return 'D:\Dev Debug\xr-sim'
}

# The folder holding the channel's commands.txt and results.txt. Given a run,
# Invoke-PreyVRLaunch.ps1's keep it in <run>\log, Start-PreyVR.ps1's beside
# PreyVR.log in the run itself. Otherwise the run with the newest PreyVR.log, as
# Send-PreyVR.ps1 picks it: it finds either layout, and the empty folder of a
# failed start cannot shadow the session that is really up.
function Find-PreyVRChannel {
    param([string]$RunDir = '')
    if ($RunDir) {
        $log = Join-Path $RunDir 'log'
        if (Test-Path -LiteralPath $log) { return $log }
        return $RunDir
    }
    $runs = Join-Path $env:LOCALAPPDATA 'PreyVR\runs'
    $latest = Get-ChildItem -LiteralPath $runs -Filter 'PreyVR.log' -Recurse -File -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw "no PreyVR.log under $runs; launch the game first" }
    return $latest.DirectoryName
}

# One command, answered or '(no result)'. A reply counts only once results.txt is
# rewritten: comparing its write time with the one before the send separates a
# fresh answer from the previous command's, the mistake that once made a sweep
# read stale values and reach a confident wrong conclusion.
function Send-PreyVRCommand {
    param([Parameter(Mandatory = $true)][string]$Channel,
          [Parameter(Mandatory = $true)][string]$Line,
          [int]$TimeoutSeconds = 25)
    $commands = Join-Path $Channel 'commands.txt'
    $results = Join-Path $Channel 'results.txt'
    $before = [datetime]::MinValue
    if (Test-Path -LiteralPath $results) { $before = (Get-Item -LiteralPath $results).LastWriteTime }
    Set-Content -LiteralPath $commands -Value $Line -Encoding ASCII
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        if ((Test-Path -LiteralPath $results) -and (Get-Item -LiteralPath $results).LastWriteTime -gt $before) {
            # Empty while the mod is still writing it: -Raw then returns $null,
            # and .Trim() on that throws under StrictMode. Keep polling.
            $text = Get-Content -LiteralPath $results -Raw
            if ($text) { return $text.Trim() }
        }
    }
    return '(no result)'
}

# One name=value field of a report line, or '?'.
function Get-ReportField {
    param([string]$Report, [string]$Name)
    if ($Report -match "(?:^|\s)$([regex]::Escape($Name))=(\S+)") { return $Matches[1] }
    return '?'
}

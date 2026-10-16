[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
# Load only the geometry, never the launcher body or the Win32 side.
foreach ($name in @('Get-PreyVRWindowFit','Resolve-PreyVRWindowSettings')) {
    $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (!$function) {throw "Missing $name"}
    Invoke-Expression $function.Extent.Text
}
function Assert-Fit($fit,$x,$y,$width,$height,$clientWidth,$clientHeight) {
    if ($fit.X -ne $x -or $fit.Y -ne $y -or $fit.Width -ne $width -or $fit.Height -ne $height -or
        $fit.ClientWidth -ne $clientWidth -or $fit.ClientHeight -ne $clientHeight) {
        throw "Unexpected window fit: $($fit | ConvertTo-Json -Compress)"
    }
}
# The reported case: 5120x1440 with a 48px taskbar, 2688x2880 eyes, 16x39 frame.
# Height-limited: the whole work area tall, at the eye's aspect, centred across.
Assert-Fit (Get-PreyVRWindowFit 0 0 5120 1392 16 39 2688 2880) 1921 0 1278 1392 1262 1353
# Width-limited on a narrow work area; the aspect ratio still holds.
Assert-Fit (Get-PreyVRWindowFit 0 0 1000 2000 16 39 2016 2160) 0 453 1000 1093 984 1054
# A screen with room to spare keeps the render size: never scaled up.
Assert-Fit (Get-PreyVRWindowFit 0 0 7680 4280 16 39 2016 2160) 2824 1040 2032 2199 2016 2160
# A secondary monitor's work area is honoured, offsets included.
Assert-Fit (Get-PreyVRWindowFit -1920 200 1920 1040 16 39 2016 2160) -1435 200 950 1040 934 1001
# **A window size from PreyVR.json** is a box for the eye-shaped picture.
# Smaller than the render size: the picture is the box's height at the eye's aspect.
Assert-Fit (Get-PreyVRWindowFit 0 0 5120 1392 16 39 2016 2160 1280 720) 2216 316 688 759 672 720
# Larger than the render size, asked for: the window may grow past it.
Assert-Fit (Get-PreyVRWindowFit 0 0 7680 4280 16 39 2016 2160 3000 3000) 2432 620 2816 3039 2800 3000
# Larger than the screen: the work area still wins.
Assert-Fit (Get-PreyVRWindowFit 0 0 2560 1392 16 39 2016 2160 4000 4000) 641 0 1278 1392 1262 1353
# 0 x 0 is the default fit, identical to passing no box.
Assert-Fit (Get-PreyVRWindowFit 0 0 5120 1392 16 39 2688 2880 0 0) 1921 0 1278 1392 1262 1353

function Assert-Window($settings,$width,$height,$minimized) {
    if ($settings.WindowWidth -ne $width -or $settings.WindowHeight -ne $height -or $settings.StartMinimized -ne $minimized) {
        throw "Unexpected window settings: $($settings | ConvertTo-Json -Compress)"
    }
}
function Assert-Rejected([scriptblock]$call,$message) {
    $rejected=$false
    try {$null=& $call} catch {$rejected=$true}
    if (!$rejected) {throw $message}
}
$defaults=@{}
foreach ($name in @('WindowWidth','WindowHeight','StartMinimized')) {
    $defaults[$name]=$ast.ParamBlock.Parameters.Where({$_.Name.VariablePath.UserPath -eq $name})[0].DefaultValue.Value
}
# Left open and fitted to the screen unless PreyVR.json says otherwise.
Assert-Window (Resolve-PreyVRWindowSettings $null @{} $defaults.WindowWidth $defaults.WindowHeight $defaults.StartMinimized) 0 0 0
$saved=[pscustomobject]@{WindowWidth=1280;WindowHeight=720;StartMinimized=$false}
Assert-Window (Resolve-PreyVRWindowSettings $saved @{} 0 0 1) 1280 720 0
# A JSON true is 1, as a hand-edited file would write it.
Assert-Window (Resolve-PreyVRWindowSettings ([pscustomobject]@{StartMinimized=$true}) @{} 0 0 0) 0 0 1
# Command-line values win over saved ones, as the other launch settings do.
Assert-Window (Resolve-PreyVRWindowSettings $saved @{WindowWidth=1;WindowHeight=1;StartMinimized=1} 800 600 1) 800 600 1
Assert-Rejected {Resolve-PreyVRWindowSettings $null @{WindowWidth=1} 800 0 1} 'One window dimension on the command line was accepted'
Assert-Rejected {Resolve-PreyVRWindowSettings ([pscustomobject]@{WindowWidth=800}) @{} 0 0 1} 'A saved width without a height was accepted'
Assert-Rejected {Resolve-PreyVRWindowSettings $null @{} 100 100 1} 'A window smaller than 160 x 120 was accepted'
Assert-Rejected {Resolve-PreyVRWindowSettings $null @{} 0 0 2} 'StartMinimized 2 was accepted'
Write-Output 'PASS: height-limited, width-limited, no upscale, offset-monitor and PreyVR.json box fits; window settings.'

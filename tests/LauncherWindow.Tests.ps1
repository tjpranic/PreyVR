[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
# Load only the geometry, never the launcher body or the Win32 side.
$function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-PreyVRWindowFit'},$true)
if (!$function) {throw 'Missing Get-PreyVRWindowFit'}
Invoke-Expression $function.Extent.Text
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
Write-Output 'PASS: height-limited, width-limited, no upscale and offset-monitor window fits.'

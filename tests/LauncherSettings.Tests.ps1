[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Extract configuration only. Never dot-source or invoke the launcher body.
$launcherFunctions=@('Resolve-PreyVRReferenceSpace','Get-PreyVRStartupCommands')
. (Join-Path $PSScriptRoot 'LauncherTestSupport.ps1')

# Reference space: a launch setting, saved in PreyVR.json.
$default=$ast.ParamBlock.Parameters.Where({$_.Name.VariablePath.UserPath -eq 'ReferenceSpace'})[0].DefaultValue.Value
if ((Resolve-PreyVRReferenceSpace $null @{} $default) -cne 'auto') {throw 'Reference space default is not auto'}
$saved=[pscustomobject]@{ReferenceSpace='local';ExtensionSetting='keep'}
if ((Resolve-PreyVRReferenceSpace $saved @{} 'auto') -cne 'local') {throw 'Saved reference space ignored'}
if ((Resolve-PreyVRReferenceSpace $saved @{ReferenceSpace=1} 'AUTO') -cne 'auto') {throw 'Explicit reference space must win, lower-cased'}
foreach ($value in @('stage','')) {
    $rejected=$false
    try {$null=Resolve-PreyVRReferenceSpace $null @{} $value} catch {$rejected=$true}
    if (!$rejected) {throw "Invalid reference space accepted: $value"}
}
$rejected=$false
try {$null=Resolve-PreyVRReferenceSpace ([pscustomobject]@{ReferenceSpace='floor'}) @{} 'auto'} catch {$rejected=$true}
if (!$rejected) {throw 'Invalid saved reference space bypassed validation'}

# **The launcher owns no VR Options setting.** No parameter, no startup command,
# no environment variable; VrMode reads none of the old variables.
$menuSettings=@('SnapTurnDegrees','HeadRelativeMovement','UiScalePercent','UiCurveDegrees','UiGuide','PointerHand','HudLayer')
$params=@($ast.ParamBlock.Parameters | ForEach-Object {$_.Name.VariablePath.UserPath})
foreach ($name in $menuSettings+@('DebugOverlay','MuzzleAim')) {
    if ($params -contains $name) {throw "The launcher still takes -$name, a VR Options setting"}
}
$commands=@(Get-PreyVRStartupCommands)
if (($commands -join "`n") -cne "input.hotkeys 1`nwindow.hold 1`nvr.enable") {throw "Startup must send only the hotkeys, the focus hold and vr.enable: $($commands -join ' | ')"}
$launcher=Get-Content -LiteralPath $scriptPath -Raw
$vrMode=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/dll/VrMode.cpp') -Raw
foreach ($variable in @('PREYVR_HUD_LAYER','PREYVR_UI_CURVE_DEGREES','PREYVR_POINTER_HAND','PREYVR_NEW_VR_OPTIONS_UI')) {
    if ($launcher -match [regex]::Escape("EnvironmentVariables['$variable']")) {throw "Launcher still passes $variable"}
    if ($vrMode -match [regex]::Escape("L`"$variable`"")) {throw "VrMode still reads $variable"}
}

# **VR Options has one panel**: no switch between panels.
if ($params -contains 'NewVROptionsUI') {throw 'The launcher still takes -NewVROptionsUI'}
Write-Output 'PASS: reference space, no VR Options setting in the launcher, no panel switch, and startup commands.'

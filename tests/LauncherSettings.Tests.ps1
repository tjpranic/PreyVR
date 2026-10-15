[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
# Extract configuration only. Never dot-source or invoke the launcher body.
foreach ($name in @('Resolve-PreyVRReferenceSpace','Move-PreyVRMenuSettings','Remove-PreyVRRetiredSettings','Get-PreyVRStartupCommands',
                    'Save-PreyVRConfiguration')) {
    $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (!$function) {throw "Missing $name"}
    Invoke-Expression $function.Extent.Text
}

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
$moved=@('SnapTurnDegrees','HeadRelativeMovement','UiScalePercent','UiCurveDegrees','UiGuide','PointerHand','HudLayer')
$params=@($ast.ParamBlock.Parameters | ForEach-Object {$_.Name.VariablePath.UserPath})
foreach ($name in $moved+@('DebugOverlay','MuzzleAim')) {
    if ($params -contains $name) {throw "The launcher still takes -$name, a VR Options setting"}
}
$commands=@(Get-PreyVRStartupCommands)
if (($commands -join "`n") -cne "input.hotkeys 1`nvr.enable") {throw "Startup must send only the hotkeys and vr.enable: $($commands -join ' | ')"}
$launcher=Get-Content -LiteralPath $scriptPath -Raw
$vrMode=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/dll/VrMode.cpp') -Raw
foreach ($variable in @('PREYVR_HUD_LAYER','PREYVR_UI_CURVE_DEGREES','PREYVR_POINTER_HAND','PREYVR_NEW_VR_OPTIONS_UI')) {
    if ($launcher -match [regex]::Escape("EnvironmentVariables['$variable']")) {throw "Launcher still passes $variable"}
    if ($vrMode -match [regex]::Escape("L`"$variable`"")) {throw "VrMode still reads $variable"}
}

# **VR Options has one panel**: no switch between panels, and a PreyVR.json
# that saved one drops it. DryRun reports and keeps it.
if ($params -contains 'NewVROptionsUI') {throw 'The launcher still takes -NewVROptionsUI'}
$old=[pscustomobject]@{NewVROptionsUI=1;DualStereo=1}
if ((@(Remove-PreyVRRetiredSettings $old $true) -join ',') -cne 'NewVROptionsUI' -or !$old.PSObject.Properties['NewVROptionsUI']) {
    throw 'DryRun must report the retired setting and keep it'
}
if ((@(Remove-PreyVRRetiredSettings $old) -join ',') -cne 'NewVROptionsUI' -or $old.PSObject.Properties['NewVROptionsUI'] -or
    !$old.PSObject.Properties['DualStereo']) {throw 'NewVROptionsUI, and only it, should have been dropped'}
if (@(Remove-PreyVRRetiredSettings $old).Count -or @(Remove-PreyVRRetiredSettings $null).Count) {throw 'Nothing was left to drop'}

# The moved settings agree with the DLL's table: key, default and range. A row
# may go on to its unit and named values.
$specs=@{}
$table=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/common/VrOptions.cpp') -Raw
foreach ($m in [regex]::Matches($table,'\{"(\w+)",L"[^"]*",L"[^"]*",(-?\d+),(-?\d+),(-?\d+),(\d+)[,}]')) {
    $specs[$m.Groups[1].Value]=@{Default=[int]$m.Groups[2].Value;Min=[int]$m.Groups[3].Value;Max=[int]$m.Groups[4].Value}
}
$keys=@{SnapTurnDegrees='turn';HeadRelativeMovement='head_relative';UiScalePercent='ui_scale';UiCurveDegrees='ui_curve';
        UiGuide='ui_guide';PointerHand='pointer_hand';HudLayer='hud_layer'}
$testRoot=Join-Path $PSScriptRoot ('../build/launcher-settings-tests/'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testRoot -Force)
$n=0
function New-Ini {$script:n++;Join-Path $testRoot "case$script:n\vr-options.ini"}
function Read-Ini($path) {[System.IO.File]::ReadAllText($path)}
foreach ($name in $moved) {
    $spec=$specs[$keys[$name]]
    if (!$spec) {throw "VrOptions.cpp has no $($keys[$name])"}
    # The default is not a choice: nothing to move.
    $ini=New-Ini
    $result=Move-PreyVRMenuSettings ([pscustomobject]@{$name=$spec.Default}) $ini
    if ($result.Moved.Count -or $result.Removed -notcontains $name -or (Test-Path -LiteralPath $ini)) {throw "$name at its default ($($spec.Default)) was moved"}
    # Each end of the range moves; one past it is dropped (the DLL would refuse the file).
    foreach ($value in @($spec.Min,$spec.Max)) {
        if ($value -eq $spec.Default -or ($name -eq 'SnapTurnDegrees' -and $value -eq 0)) {continue}
        $ini=New-Ini
        $result=Move-PreyVRMenuSettings ([pscustomobject]@{$name=$value}) $ini
        if ((Read-Ini $ini) -cne "version=1`n$($keys[$name])=$value`n") {throw "$name=$value did not move to $($keys[$name])"}
    }
    foreach ($value in @(($spec.Min-1),($spec.Max+1))) {
        $ini=New-Ini
        $result=Move-PreyVRMenuSettings ([pscustomobject]@{$name=$value}) $ini
        if ($result.Moved.Count -or (Test-Path -LiteralPath $ini)) {throw "$name=$value is out of range and was moved"}
    }
}
# Turning: smooth (0) moves, 1..14 are not angles the DLL takes.
$ini=New-Ini
$null=Move-PreyVRMenuSettings ([pscustomobject]@{SnapTurnDegrees=0}) $ini
if ((Read-Ini $ini) -cne "version=1`nturn=0`n") {throw 'Smooth turning did not move'}
$ini=New-Ini
if ((Move-PreyVRMenuSettings ([pscustomobject]@{SnapTurnDegrees=14}) $ini).Moved.Count) {throw 'A 14 degree snap was moved'}

# A player's PreyVR.json, every setting chosen, no vr-options.ini yet.
$player=[pscustomobject]@{GameExe='x';Width=2688;Height=2880;SnapTurnDegrees=30;HeadRelativeMovement=0;UiScalePercent=130;
    UiCurveDegrees=0;UiGuide=1;PointerHand=0;HudLayer=0;ReferenceSpace='local';DualStereo=1;ExtensionSetting='keep'}
$ini=New-Ini
$preview=Move-PreyVRMenuSettings $player $ini $true
if ($preview.Moved.Count -ne 7 -or (Test-Path -LiteralPath $ini) -or !$player.PSObject.Properties['UiScalePercent']) {throw 'DryRun must report and change nothing'}
$result=Move-PreyVRMenuSettings $player $ini
$expected="version=1`nturn=30`nhead_relative=0`nui_scale=130`nui_curve=0`nui_guide=1`npointer_hand=0`nhud_layer=0`n"
if ((Read-Ini $ini) -cne $expected) {throw "Unexpected vr-options.ini:`n$(Read-Ini $ini)"}
$bytes=[System.IO.File]::ReadAllBytes($ini)
if ($bytes[0] -ne [byte][char]'v' -or ($bytes -contains 13)) {throw 'vr-options.ini must be the DLL''s form: no BOM, LF only'}
if ($result.Moved.Count -ne 7 -or $result.Removed.Count) {throw 'Every chosen setting should have moved'}
foreach ($name in $moved) {if ($player.PSObject.Properties[$name]) {throw "$name is still in PreyVR.json"}}
foreach ($name in @('GameExe','Width','Height','ReferenceSpace','DualStereo','ExtensionSetting')) {
    if (!$player.PSObject.Properties[$name]) {throw "Moving the menu settings lost $name"}
}
if (Test-Path -LiteralPath ($ini+'.tmp')) {throw 'Temporary file left behind'}
# Saved without them, and a second launch has nothing left to move.
$config=Join-Path (Split-Path -Parent $ini) 'PreyVR.json'
Save-PreyVRConfiguration $config $player @{Width=2688} $false
$reloaded=Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
foreach ($name in $moved) {if ($reloaded.PSObject.Properties[$name]) {throw "$name came back in the saved PreyVR.json"}}
$again=Move-PreyVRMenuSettings $reloaded $ini
if ($again.Moved.Count -or $again.Removed.Count -or (Read-Ini $ini) -cne $expected) {throw 'A second launch moved something again'}

# The menu's saved choice wins: an existing vr-options.ini keeps its lines and
# order, and only settings it lacks are added.
$ini=New-Ini
[void](New-Item -ItemType Directory -Path (Split-Path -Parent $ini) -Force)
[System.IO.File]::WriteAllText($ini,"version=1`nturn=60`nui_scale=150`nwrist_on_hud=1`n",[System.Text.Encoding]::ASCII)
$result=Move-PreyVRMenuSettings ([pscustomobject]@{SnapTurnDegrees=30;UiScalePercent=130;PointerHand=2;UiCurveDegrees=99}) $ini
if ((Read-Ini $ini) -cne "version=1`nturn=60`nui_scale=150`nwrist_on_hud=1`npointer_hand=2`n") {throw "Existing choices were not kept:`n$(Read-Ini $ini)"}
if (($result.Moved -join ',') -cne 'PointerHand=2' -or ($result.Removed -join ',') -cne 'SnapTurnDegrees,UiScalePercent,UiCurveDegrees') {
    throw "Unexpected report: moved $($result.Moved -join ','); removed $($result.Removed -join ',')"
}
# Nothing to move: the file is not touched.
$ini=New-Ini
[void](New-Item -ItemType Directory -Path (Split-Path -Parent $ini) -Force)
[System.IO.File]::WriteAllText($ini,"version=1`nturn=60`n",[System.Text.Encoding]::ASCII)
$before=(Get-Item -LiteralPath $ini).LastWriteTimeUtc
$null=Move-PreyVRMenuSettings ([pscustomobject]@{HudLayer=1;ExtensionSetting='keep'}) $ini
if ((Get-Item -LiteralPath $ini).LastWriteTimeUtc -ne $before -or (Read-Ini $ini) -cne "version=1`nturn=60`n") {throw 'A file with nothing to add was rewritten'}
if ((Move-PreyVRMenuSettings $null $ini).Moved.Count) {throw 'No PreyVR.json, nothing to move'}
Write-Output 'PASS: reference space, no VR Options setting in the launcher, the retired panel switch dropped, startup commands, and the one-time move into vr-options.ini (DLL keys, defaults and ranges; menu choices kept; DryRun; idempotence).'

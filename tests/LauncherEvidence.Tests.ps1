[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Extract the function only. Never dot-source or invoke the launcher body.
$launcherFunctions=@('Save-PreyVRPreviousRunEvidence')
. (Join-Path $PSScriptRoot 'LauncherTestSupport.ps1')

$root=Join-Path ([IO.Path]::GetTempPath()) ('preyvr-evidence-'+[guid]::NewGuid().ToString('N'))
$game=Join-Path $root 'game'
$runs=Join-Path $root 'runs'
[void](New-Item -ItemType Directory -Path $game)
try {
    if (@(Save-PreyVRPreviousRunEvidence -GameRoot $game -RunRoot $runs).Count) {throw 'No run folder yet, yet something was copied'}
    [void](New-Item -ItemType Directory -Path $runs)
    if (@(Save-PreyVRPreviousRunEvidence -GameRoot $game -RunRoot $runs).Count) {throw 'No previous run, yet something was copied'}

    $older=New-Item -ItemType Directory -Path (Join-Path $runs 'player-20261025-161415')
    $previous=New-Item -ItemType Directory -Path (Join-Path $runs 'player-20261025-163504')
    $stray=New-Item -ItemType Directory -Path (Join-Path $runs 'run-20261026-000000') # not a player run
    $older.CreationTime=(Get-Date).AddHours(-3)
    $previous.CreationTime=(Get-Date).AddMinutes(-30)
    Set-Content -LiteralPath (Join-Path $game 'Game.log') -Value 'Begin handle Exception'
    # error.log from an earlier session: written before the previous run began.
    Set-Content -LiteralPath (Join-Path $game 'error.log') -Value 'old'
    (Get-Item -LiteralPath (Join-Path $game 'error.log')).LastWriteTime=(Get-Date).AddHours(-2)

    $copied=@(Save-PreyVRPreviousRunEvidence -GameRoot $game -RunRoot $runs)
    if (($copied -join ',') -cne 'Game.log') {throw "Expected Game.log only, got: $($copied -join ',')"}
    if ((Get-Content -LiteralPath (Join-Path $previous.FullName 'Game.log') -Raw) -notmatch 'Begin handle Exception') {throw 'Game.log not copied into the newest player run'}
    foreach ($folder in @($older,$stray)) {
        if (Test-Path -LiteralPath (Join-Path $folder.FullName 'Game.log')) {throw "Copied into $($folder.Name)"}
    }
    if (Test-Path -LiteralPath (Join-Path $previous.FullName 'error.log')) {throw 'An earlier session''s error.log was taken for this one'}

    if (@(Save-PreyVRPreviousRunEvidence -GameRoot $game -RunRoot $runs).Count) {throw 'Copied again over what is already there'}

    Set-Content -LiteralPath (Join-Path $game 'error.dmp') -Value 'MDMP'
    $copied=@(Save-PreyVRPreviousRunEvidence -GameRoot $game -RunRoot $runs)
    if (($copied -join ',') -cne 'error.dmp') {throw "Expected the new error.dmp, got: $($copied -join ',')"}
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

# The launcher calls it before it makes the new run folder, from the game root.
$launcher=Get-Content -LiteralPath $scriptPath -Raw
$call=$launcher.IndexOf('Save-PreyVRPreviousRunEvidence -GameRoot $gameRoot -RunRoot $RunRoot')
$newRun=$launcher.IndexOf("`$run=Join-Path `$RunRoot ('player-")
if ($call -lt 0 -or $newRun -lt 0 -or $call -gt $newRun) {throw 'The launcher must keep the last run''s evidence before it starts a new run'}
Write-Output 'PASS: previous-run evidence: newest player run only, written-since check, no overwrite, never fatal, called before the new run.'

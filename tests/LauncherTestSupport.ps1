# The launcher tests' shared start. Dot-source it after setting
# $launcherFunctions: it parses tools/Start-PreyVR.ps1 into $ast ($scriptPath is
# its path) and defines only the named functions from it, in the test's scope.
# Never dot-source or invoke the launcher body.
$scriptPath=Join-Path $PSScriptRoot '../tools/Start-PreyVR.ps1'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) {throw ($parseErrors | Out-String)}
foreach ($name in $launcherFunctions) {
    $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (!$function) {throw "Missing $name"}
    Invoke-Expression $function.Extent.Text
}

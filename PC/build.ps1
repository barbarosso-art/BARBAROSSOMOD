[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstancePath,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $projectDir 'BARBAROSSOMOD.cs'
$manifest = Join-Path $projectDir 'BARBAROSSOMOD.manifest.json'
$output = Join-Path $projectDir 'BARBAROSSOMOD.dll'
$bundle = Join-Path $projectDir 'Assets\BARBAROSSOMODAssets.bundle'
$managed = Join-Path $InstancePath 'Beat Saber_Data\Managed'
$libs = Join-Path $InstancePath 'Libs'
$plugins = Join-Path $InstancePath 'Plugins'
$csc = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'

$references = @(
    (Join-Path $managed 'IPA.Loader.dll'),
    (Join-Path $managed 'Main.dll'),
    (Join-Path $managed 'DataModels.dll'),
    (Join-Path $managed 'UnityEngine.CoreModule.dll'),
    (Join-Path $managed 'UnityEngine.AssetBundleModule.dll'),
    (Join-Path $managed 'UnityEngine.ImageConversionModule.dll'),
    (Join-Path $managed 'UnityEngine.PhysicsModule.dll'),
    (Join-Path $managed 'Zenject.dll'),
    (Join-Path $managed 'Zenject-usage.dll'),
    (Join-Path $plugins 'SiraUtil.dll'),
    (Join-Path $libs 'Newtonsoft.Json.dll'),
    (Join-Path $libs 'netstandard.dll')
)
$required = @($csc, $source, $manifest, $bundle, (Join-Path $InstancePath 'Beat Saber.exe')) + $references
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required build file was not found: $path"
    }
}

$arguments = @('/nologo', '/target:library', '/optimize+', '/debug-', "/out:$output", "/resource:$manifest,BARBAROSSOMOD.manifest.json")
$arguments += $references | ForEach-Object { "/reference:$_" }
$arguments += $source
& $csc @arguments
if ($LASTEXITCODE -ne 0) {
    throw "C# compiler failed with exit code $LASTEXITCODE"
}
Write-Host "[OK] Built: $output"

if ($Install) {
    $backupDir = Join-Path $projectDir ('backups\installed_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    foreach ($name in @('MapImagePC.dll', 'BARBAROSSOMOD.dll', 'MapImagePCAssets.bundle')) {
        $current = Join-Path $plugins $name
        if (Test-Path -LiteralPath $current -PathType Leaf) {
            Copy-Item -LiteralPath $current -Destination $backupDir -Force
        }
    }
    $oldPlugin = Join-Path $plugins 'MapImagePC.dll'
    if (Test-Path -LiteralPath $oldPlugin -PathType Leaf) {
        $disabled = Join-Path $InstancePath 'DisabledPlugins\BARBAROSSOMOD_v0.1_upgrade'
        New-Item -ItemType Directory -Path $disabled -Force | Out-Null
        Move-Item -LiteralPath $oldPlugin -Destination (Join-Path $disabled 'MapImagePC.dll') -Force
    }
    Copy-Item -LiteralPath $output -Destination (Join-Path $plugins 'BARBAROSSOMOD.dll') -Force
    Copy-Item -LiteralPath $bundle -Destination (Join-Path $plugins 'MapImagePCAssets.bundle') -Force
    Write-Host "[OK] Installed BARBAROSSOMOD v0.1 into: $plugins"
}

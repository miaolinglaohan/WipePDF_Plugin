# WipePDF Plugin Installer for Adobe Acrobat Pro (64-bit)
param(
    [string]$PluginPath = "$PSScriptRoot\..\build\WipePDF.api"
)

# Elevate if not running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Requesting Administrator privileges to deploy into Program Files..." -ForegroundColor Yellow
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    if ($PluginPath) {
        $argList += " -PluginPath `"$PluginPath`""
    }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait
    exit
}

Write-Host "Searching for Adobe Acrobat installation..." -ForegroundColor Cyan

$acrobatPaths = @(
    "C:\Program Files\Adobe\Acrobat DC\Acrobat",
    "C:\Program Files\Adobe\Acrobat\Acrobat",
    "C:\Program Files (x86)\Adobe\Acrobat DC\Acrobat",
    "C:\Program Files (x86)\Adobe\Acrobat\Acrobat"
)

$targetDir = $null
foreach ($p in $acrobatPaths) {
    $pluginDir = Join-Path $p "plug_ins"
    if (Test-Path $pluginDir) {
        $targetDir = $pluginDir
        break
    }
}

if (-not $targetDir) {
    Write-Warning "Could not automatically locate Acrobat plug_ins directory."
    exit 1
}

$resolvedPlugin = (Resolve-Path $PluginPath -ErrorAction SilentlyContinue).Path
if (-not $resolvedPlugin -or -not (Test-Path $resolvedPlugin)) {
    Write-Error "Source plugin not found at: $PluginPath"
    exit 1
}

$destPath = Join-Path $targetDir "WipePDF.api"
Write-Host "Found Acrobat plug_ins directory: $targetDir" -ForegroundColor Green
Write-Host "Installing $resolvedPlugin -> $destPath ..." -ForegroundColor Cyan

Copy-Item -Force $resolvedPlugin $destPath

if (Test-Path $destPath) {
    $size = (Get-Item $destPath).Length
    Write-Host "=================================================" -ForegroundColor Green
    Write-Host " WipePDF.api successfully installed! ($size bytes) " -ForegroundColor Green
    Write-Host " Please start or restart Adobe Acrobat to use it. " -ForegroundColor Green
    Write-Host "=================================================" -ForegroundColor Green
} else {
    Write-Error "Failed to install WipePDF.api"
}

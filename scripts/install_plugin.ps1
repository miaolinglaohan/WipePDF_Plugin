param(
    [string]$PluginPath = "..\build_x64\WipePDF.api"
)

Write-Host "Searching for Adobe Acrobat installation..." -ForegroundColor Cyan

$acrobatPaths = @(
    "C:\Program Files\Adobe\Acrobat DC\Acrobat",
    "C:\Program Files (x86)\Adobe\Acrobat DC\Acrobat",
    "C:\Program Files\Adobe\Acrobat\Acrobat",
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
    Write-Warning "Could not automatically locate Acrobat plug_ins folder."
    Write-Host "Please manually copy WipePDF.api to your Acrobat plug_ins directory."
    exit 1
}

Write-Host "Found Acrobat plug_ins at: $targetDir" -ForegroundColor Green
Copy-Item -Force $PluginPath (Join-Path $targetDir "WipePDF.api")
Write-Host "WipePDF.api successfully installed!" -ForegroundColor Green

param(
    [string]$BuildType = "Release",
    [switch]$Clean
)

Set-Location -Path "D:\AI\AIProjects\WipePDF_Plugin"

$vsPath = "D:\AI\Programs\Microsoft Visual Studio\2022\BuildTools"
$cmakePath = "D:\Qt\Tools\CMake_64\bin\cmake.exe"
$ninjaDir = "D:\AI\Programs\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$ninjaExe = "D:\AI\Programs\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if ($Clean -and (Test-Path "D:\AI\AIProjects\WipePDF_Plugin\build")) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "D:\AI\AIProjects\WipePDF_Plugin\build"
}

$cmd = "cd /d `"D:\AI\AIProjects\WipePDF_Plugin`" && " + `
       "set `"PATH=$ninjaDir;%PATH%`" && " + `
       "call `"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" && " + `
       "`"$cmakePath`" -B build -G Ninja -DCMAKE_BUILD_TYPE=$BuildType " + `
       "-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl " + `
       "-DCMAKE_MAKE_PROGRAM=`"$ninjaExe`" && " + `
       "`"$cmakePath`" --build build"

cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Host "Build completed successfully!" -ForegroundColor Green

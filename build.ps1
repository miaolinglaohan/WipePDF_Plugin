param(
    [string]$BuildType = "Release",
    [switch]$Clean
)

$vsPath = "D:\AI\Programs\Microsoft Visual Studio\2022\BuildTools"
$qtPath = "D:\Qt\6.5.3\msvc2019_64"
$cmakePath = "D:\Qt\Tools\CMake_64\bin\cmake.exe"
$ninjaDir = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$ninjaExe = "$ninjaDir\ninja.exe"

if ($Clean -and (Test-Path "build")) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}

$cmd = "set `"PATH=$qtPath\bin;$ninjaDir;%PATH%`" && " +
       "call `"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" && " +
       "`"$cmakePath`" -B build -G Ninja -DCMAKE_BUILD_TYPE=$BuildType " +
       "-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl " +
       "-DCMAKE_PREFIX_PATH=`"$qtPath`" " +
       "-DCMAKE_MAKE_PROGRAM=`"$ninjaExe`" && " +
       "`"$cmakePath`" --build build"

cmd /c $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Host "Build completed successfully!" -ForegroundColor Green

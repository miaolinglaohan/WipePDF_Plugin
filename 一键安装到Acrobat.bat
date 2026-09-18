@echo off
chcp 65001 >nul
title WipePDF Acrobat 插件一键安装器

:: 检查是否具有管理员权限
net session >nul 2>&1
if %errorlevel% neq 0 goto ELEVATE

set "SRC=%~dp0build\WipePDF.api"
if not exist "%SRC%" set "SRC=%~dp0..\build\WipePDF.api"
if not exist "%SRC%" set "SRC=D:\AI\AIProjects\WipePDF_Plugin\build\WipePDF.api"

set "DST=C:\Program Files\Adobe\Acrobat DC\Acrobat\plug_ins"

if not exist "%SRC%" goto ERR_SRC
if not exist "%DST%" goto ERR_DST

echo 正在复制 WipePDF.api 到 Acrobat 插件目录...
copy /Y "%SRC%" "%DST%\WipePDF.api" >nul
if %errorlevel% neq 0 goto ERR_COPY

echo.
echo ========================================================
echo  恭喜！WipePDF 插件已成功安装到 Adobe Acrobat Pro！
echo  插件路径：%DST%\WipePDF.api
echo  核心导出函数：PlugInMain (已验证)
echo.
echo  现在启动 Adobe Acrobat Pro 即可在：
echo   1. 顶部菜单栏看到独立的【WipePDF】主菜单
echo   2. 【编辑 (Edit)】菜单底部看到【WipePDF 水印清理...】
echo ========================================================
echo.
pause
exit /b 0

:ELEVATE
echo 正在请求管理员权限，请在弹出的系统提示中点击“是”...
powershell -NoProfile -Command "Start-Process cmd -ArgumentList '/c `\"%~f0`\"' -Verb RunAs"
exit /b

:ERR_SRC
echo [错误] 未找到插件编译产物：%SRC%
pause
exit /b 1

:ERR_DST
echo [错误] 未找到 Acrobat 插件目录：%DST%
pause
exit /b 1

:ERR_COPY
echo [错误] 复制文件失败，请确认 Adobe Acrobat 是否已完全关闭后重试。
pause
exit /b 1

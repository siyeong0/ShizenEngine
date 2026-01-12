@echo off
setlocal enabledelayedexpansion

REM ============================================
REM Visual Studio PlatformToolset Retarget Script
REM Usage:
REM   retarget_toolset.bat v142
REM   retarget_toolset.bat v143
REM ============================================

if "%1"=="" (
    echo Usage: %~nx0 v142 ^| v143
    exit /b 1
)

set TARGET_TOOLSET=%1

if NOT "%TARGET_TOOLSET%"=="v142" if NOT "%TARGET_TOOLSET%"=="v143" (
    echo Invalid toolset: %TARGET_TOOLSET%
    echo Allowed values: v142, v143
    exit /b 1
)

echo.
echo ============================================
echo Retargeting all .vcxproj to %TARGET_TOOLSET%
echo ============================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass ^
  "Get-ChildItem -Recurse -Filter *.vcxproj |" ^
  "ForEach-Object {" ^
  "  (Get-Content $_.FullName) " ^
  "    -replace '<PlatformToolset>v142</PlatformToolset>', '<PlatformToolset>%TARGET_TOOLSET%</PlatformToolset>' " ^
  "    -replace '<PlatformToolset>v143</PlatformToolset>', '<PlatformToolset>%TARGET_TOOLSET%</PlatformToolset>' " ^
  "    | Set-Content $_.FullName" ^
  "}"

echo.
echo Done.
pause

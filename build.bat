@echo off
rem ============================================================
rem  PromDock build script (MSVC)
rem    build.bat        : build  -> build\PromDock.exe (promdock.c + launcher.c + setting.c + searchbar.c を1つのexeに)
rem    build.bat run    : build and launch
rem
rem  Works in any of these situations:
rem    1) Run inside "x64 Native Tools Command Prompt for VS"
rem       (cl.exe is already on PATH -> compiles right away)
rem    2) Double-click / plain cmd: Visual Studio or Build Tools is
rem       located automatically (vswhere, then common install paths)
rem ============================================================
setlocal
cd /d "%~dp0"

set "SRC=promdock.c"
set "RC=0"
set "VCVARS="

if not exist "%SRC%" (
    echo [ERROR] %SRC% not found next to build.bat.
    set "RC=1"
    goto :done
)
if not exist "launcher.c" (
    echo [ERROR] launcher.c not found next to build.bat.
    set "RC=1"
    goto :done
)
if not exist "setting.c" (
    echo [ERROR] setting.c not found next to build.bat.
    set "RC=1"
    goto :done
)
if not exist "searchbar.c" (
    echo [ERROR] searchbar.c not found next to build.bat.
    set "RC=1"
    goto :done
)

rem --- 1) already in a developer prompt? ---
where cl >nul 2>nul
if not errorlevel 1 goto :compile

rem --- 2) find vcvars64.bat ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
call :vsw -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if not defined VCVARS call :vsw -latest -products * -property installationPath
if not defined VCVARS call :scanroot "%ProgramFiles%"
if not defined VCVARS call :scanroot "%ProgramFiles(x86)%"
if not defined VCVARS goto :novs

echo Using: %VCVARS%
call "%VCVARS%" >nul
where cl >nul 2>nul
if errorlevel 1 goto :novs
goto :compile

:novs
echo [ERROR] MSVC (cl.exe) was not found.
echo.
echo   Easiest fix: open "x64 Native Tools Command Prompt for VS" from the
echo   Start menu, cd to this folder, and run build.bat there.
echo.
echo   If that prompt is not in the Start menu, install "Visual Studio Build
echo   Tools" (or Visual Studio) with the "Desktop development with C++"
echo   workload.
echo.
if exist "%VSWHERE%" (echo   [info] vswhere: found) else echo   [info] vswhere: NOT found
set "RC=1"
goto :done

:compile
if not exist build mkdir build

rem --- a running PromDock locks the exe, so close it first ---
taskkill /f /im PromDock.exe >nul 2>nul

rem --- ああああ ---
set "RES="
if exist "promdock.ico" if exist "promdock.rc" (
    rc /nologo /fo build\promdock.res promdock.rc
    if errorlevel 1 (
        echo [ERROR] Icon resource compile failed.
        set "RC=1"
        goto :done
    )
    set "RES=build\promdock.res"
) else (
    echo [info] promdock.ico / promdock.rc not found - building with the built-in icon.
)

echo Building PromDock ...
cl /nologo /O2 /W3 /utf-8 /Fobuild\ /Febuild\PromDock.exe "%SRC%" launcher.c setting.c searchbar.c %RES% /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    set "RC=1"
    goto :done
)

echo.
echo [OK] build\PromDock.exe
if /i "%~1"=="run" start "" "build\PromDock.exe"

:done
rem --- keep the window open when launched by double-click ---
echo %cmdcmdline% | find /i "/c" >nul
if not errorlevel 1 pause
endlocal & exit /b %RC%

rem ------------------------------------------------------------
rem  :vsw  <vswhere args>  -> sets VCVARS if an install is found
:vsw
if not exist "%VSWHERE%" exit /b
for /f "usebackq delims=" %%i in (`"%VSWHERE%" %*`) do if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
exit /b

rem  :scanroot  <ProgramFiles dir>  -> sets VCVARS from common paths (newest first)
:scanroot
for %%Y in (18 2026 2022 2019 2017) do for %%E in (Enterprise Professional Community BuildTools Preview) do if not defined VCVARS if exist "%~1\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%~1\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
exit /b 

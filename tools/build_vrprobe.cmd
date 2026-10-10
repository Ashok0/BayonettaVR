@echo off
REM Builds vrprobe.exe into build\x64\Release next to the mod.
REM
REM No parenthesised blocks: %ProgramFiles(x86)% contains parentheses and breaks
REM cmd's block parsing even when quoted.
setlocal

where cl.exe >nul 2>&1
if not errorlevel 1 goto have_cl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs
"%VSWHERE%" -latest -products * -property installationPath > "%TEMP%\_trvr_vspath3.txt" 2>nul
set "VSPATH="
set /p VSPATH=<"%TEMP%\_trvr_vspath3.txt"
del "%TEMP%\_trvr_vspath3.txt" >nul 2>&1
if not defined VSPATH goto no_vs
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>&1
if errorlevel 1 goto no_vs

:have_cl
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\x64\Release"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"

cl /nologo /std:c++17 /EHsc /W4 /MT /O2 /DWIN32_LEAN_AND_MEAN /I "%ROOT%\third_party\openvr\headers" "%ROOT%\tools\vrprobe.cpp" /Fe:vrprobe.exe >nul
if errorlevel 1 goto build_failed

echo Built %OUT%\vrprobe.exe
echo Copy it into the game folder and run it there.
popd
exit /b 0

:build_failed
popd
echo Build failed.
exit /b 1

:no_vs
echo Could not locate a Visual Studio x64 toolchain. Run from a developer prompt.
exit /b 1

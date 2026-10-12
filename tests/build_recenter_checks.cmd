@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\recenter_tests.cpp /Fobuild\test\recenter_tests.obj /Febuild\test\recenter_tests.exe
if errorlevel 1 exit /b 1
build\test\recenter_tests.exe
exit /b %errorlevel%

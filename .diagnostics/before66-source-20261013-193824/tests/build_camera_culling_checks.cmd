@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\camera_culling_tests.cpp src\CameraCulling.cpp /Fobuild\test\ /Febuild\test\camera_culling_tests.exe
if errorlevel 1 exit /b 1
build\test\camera_culling_tests.exe
exit /b %errorlevel%

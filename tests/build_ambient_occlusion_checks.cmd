@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\ambient_occlusion_tests.cpp src\DepthOfField.cpp /Fobuild\test\ /Febuild\test\ambient_occlusion_tests.exe /link d3d9.lib user32.lib
if errorlevel 1 exit /b 1
build\test\ambient_occlusion_tests.exe
exit /b %errorlevel%

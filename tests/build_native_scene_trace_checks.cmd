@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\native_scene_trace_tests.cpp /Fobuild\test\native_scene_trace_tests.obj /Febuild\test\native_scene_trace_tests.exe /link d3d9.lib d3dcompiler.lib user32.lib
if errorlevel 1 exit /b 1
build\test\native_scene_trace_tests.exe
exit /b %errorlevel%

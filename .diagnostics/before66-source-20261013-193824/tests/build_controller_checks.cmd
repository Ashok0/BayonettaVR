@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc tests\controller_input_tests.cpp /Fobuild\test\controller_input_tests.obj /Febuild\test\controller_input_tests.exe
if errorlevel 1 exit /b 1
build\test\controller_input_tests.exe
if errorlevel 1 exit /b 1
lib /nologo /machine:x86 /def:tests\fixtures\xinput1_3.def /out:build\test\xinput1_3_test.lib
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc tests\controller_hook_tests.cpp src\ControllerHooks.cpp /Fobuild\test\ /Febuild\test\controller_hook_tests.exe /link build\test\xinput1_3_test.lib
if errorlevel 1 exit /b 1
build\test\controller_hook_tests.exe
if errorlevel 1 exit /b 1
exit /b 0

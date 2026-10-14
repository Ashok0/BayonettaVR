@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\first_person_tests.cpp /Fobuild\test\first_person_tests.obj /Febuild\test\first_person_tests.exe
if not "%errorlevel%"=="0" exit /b 1
build\test\first_person_tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\first_person_hook_tests.cpp src\FirstPerson.cpp /Fobuild\test\ /Febuild\test\first_person_hook_tests.exe
if not "%errorlevel%"=="0" exit /b 1
build\test\first_person_hook_tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\first_person_gpu_tests.cpp /Fobuild\test\first_person_gpu_tests.obj /Febuild\test\first_person_gpu_tests.exe /link d3d9.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\test\first_person_gpu_tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\first_person_costume_gpu_tests.cpp /Fobuild\test\first_person_costume_gpu_tests.obj /Febuild\test\first_person_costume_gpu_tests.exe /link d3d9.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\test\first_person_costume_gpu_tests.exe
exit /b %errorlevel%

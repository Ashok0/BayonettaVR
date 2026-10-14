@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
if not exist build\test\race69 mkdir build\test\race69
cl /nologo /O2 /EHsc /std:c++17 /W4 /MT /Isrc tests\first_person_hook_tests.cpp .diagnostics\before70-source-20261013-233810\FirstPerson.cpp /Fobuild\test\race69\ /Febuild\test\race69\old.exe
if errorlevel 1 exit /b 1
build\test\race69\old.exe
if not errorlevel 42 exit /b 1
if errorlevel 43 exit /b 1
echo Old .69 correctly failed the concurrent frame reset regression.
if not exist build\test\race70 mkdir build\test\race70
cl /nologo /O2 /EHsc /std:c++17 /W4 /MT /Isrc tests\first_person_hook_tests.cpp src\FirstPerson.cpp /Fobuild\test\race70\ /Febuild\test\race70\fixed.exe
if errorlevel 1 exit /b 1
build\test\race70\fixed.exe
exit /b %errorlevel%

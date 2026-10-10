@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /W4 /MT .diagnostics\independent31-result-20261002\fpu_probe.cpp /Fo.diagnostics\independent31-result-20261002\fpu_probe.obj /Fe.diagnostics\independent31-result-20261002\fpu_probe.exe /link user32.lib
if errorlevel 1 exit /b 1
.diagnostics\independent31-result-20261002\fpu_probe.exe

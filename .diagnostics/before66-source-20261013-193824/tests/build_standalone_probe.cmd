@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc /Ithird_party\openvr\headers tests\standalone_tracking_probe.cpp src\ReferenceGridRenderer.cpp /Fobuild\test\ /Febuild\test\standalone_tracking_probe.exe /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib
exit /b %errorlevel%

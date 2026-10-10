@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
if not exist build\companion mkdir build\companion
cl /nologo /O2 /EHsc /std:c++17 /W4 /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc /Ithird_party\openvr\headers src\CompanionHost.cpp src\SharedFrameTransport.cpp src\ReferenceGridRenderer.cpp /Fobuild\companion\ /Febuild\Win32\Release\BayonettaVRHost.exe /link d3d11.lib dxgi.lib d3dcompiler.lib
exit /b %errorlevel%

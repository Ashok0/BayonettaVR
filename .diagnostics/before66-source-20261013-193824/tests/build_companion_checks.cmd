@echo off
setlocal
call tools\build_companion.cmd
if errorlevel 1 exit /b 1
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /W4 /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc /Ithird_party\openvr\headers tests\companion_transport_tests.cpp src\CompanionClient.cpp src\SharedFrameTransport.cpp /Fobuild\companion\ /Febuild\test\companion_transport_tests.exe /link d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1
build\test\companion_transport_tests.exe
exit /b %errorlevel%

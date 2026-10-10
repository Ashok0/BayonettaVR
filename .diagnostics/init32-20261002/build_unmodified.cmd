@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc /Ithird_party\openvr\headers tests\standalone_tracking_probe.cpp src\ReferenceGridRenderer.cpp /Fo.diagnostics\init32-20261002\ /Fe.diagnostics\init32-20261002\unmodified_probe.exe /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib
exit /b %errorlevel%

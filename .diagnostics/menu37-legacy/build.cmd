@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /W4 /MT /I.diagnostics\menu37-legacy /Isrc tests\menu_panel_tests.cpp .diagnostics\menu37-legacy\MenuPanel9.cpp /Fo.diagnostics\menu37-legacy\ /Fe.diagnostics\menu37-legacy\legacy_menu_tests.exe /link d3d9.lib d3dcompiler.lib user32.lib
if errorlevel 1 exit /b 1
.diagnostics\menu37-legacy\legacy_menu_tests.exe

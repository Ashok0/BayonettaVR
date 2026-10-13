@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\menu_feedback_tests.cpp src\MenuPanel9.cpp /Fobuild\test\ /Febuild\test\menu_feedback_tests.exe /link d3d9.lib d3dcompiler.lib user32.lib
if errorlevel 1 exit /b 1
build\test\menu_feedback_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\shader_analysis_tests.cpp /Fobuild\test\shader_analysis_tests.obj /Febuild\test\shader_analysis_tests.exe
if errorlevel 1 exit /b 1
build\test\shader_analysis_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\reference_grid_tests.cpp src\ReferenceGridRenderer.cpp /Fobuild\test\ /Febuild\test\reference_grid_tests.exe /link d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
build\test\reference_grid_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\pose_submission_tests.cpp /Fobuild\test\pose_submission_tests.obj /Febuild\test\pose_submission_tests.exe
if errorlevel 1 exit /b 1
build\test\pose_submission_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\frame_loop_worker_tests.cpp /Fobuild\test\frame_loop_worker_tests.obj /Febuild\test\frame_loop_worker_tests.exe
if errorlevel 1 exit /b 1
build\test\frame_loop_worker_tests.exe
exit /b %errorlevel%

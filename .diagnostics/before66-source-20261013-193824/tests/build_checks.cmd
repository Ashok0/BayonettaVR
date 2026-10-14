@echo off
call tests\build_practice_reflection_checks.cmd
if errorlevel 1 exit /b 1
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc /Ithird_party\openvr\headers tests\projection_probe.cpp /Fobuild\test\projection_probe.obj /Febuild\test\projection_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\matrix_tests.cpp /Fobuild\test\matrix_tests.obj /Febuild\test\matrix_tests.exe
if errorlevel 1 exit /b 1
build\test\matrix_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\effect_stream_tests.cpp /Fobuild\test\effect_stream_tests.obj /Febuild\test\effect_stream_tests.exe
if errorlevel 1 exit /b 1
build\test\effect_stream_tests.exe
if errorlevel 1 exit /b 1
call tests\build_controller_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_dof_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_ambient_occlusion_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_color_grading_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_soft_particle_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_movie_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_scene_capture_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_native_scene_trace_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_map_cinema_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_camera_culling_checks.cmd
if errorlevel 1 exit /b 1
call tests\build_recenter_checks.cmd
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\world_hud_tests.cpp /Fobuild\test\world_hud_tests.obj /Febuild\test\world_hud_tests.exe
if errorlevel 1 exit /b 1
build\test\world_hud_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /Isrc tests\menu_panel_tests.cpp src\MenuPanel9.cpp /Fobuild\test\ /Febuild\test\menu_panel_tests.exe /link d3d9.lib d3dcompiler.lib user32.lib
if errorlevel 1 exit /b 1
build\test\menu_panel_tests.exe
if errorlevel 1 exit /b 1
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

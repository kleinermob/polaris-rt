@echo off
cd /d "%~dp0"
set "VK_LAYER_PATH=%CD%"
set "VK_LOADER_DEBUG=all"
rt_test_app.exe

@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-native-replay.ps1" %*
if errorlevel 1 pause

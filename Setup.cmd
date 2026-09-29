@echo off
rem genrecomp quick start: checks prerequisites, builds, runs the self-check.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" %*
if errorlevel 1 (pause & exit /b 1)
pause
